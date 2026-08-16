// ============================================================================
// Native unit tests for CommandQueue (Lot 2 - scheduler hardening)
// ============================================================================
// Covers the P0/P1 fixes:
//   #4  time-ordered dispatch (immediate command not blocked by a queued
//       later command)
//   #5  every operation is guarded (exercised indirectly; the spinlock is a
//       no-op on the host, logic must still be correct)
//   #6  sorted-by-execute_at insertion + micros() wraparound ordering
//   #7  transactional ON/OFF insertion (all-or-nothing)
// ============================================================================

#include <unity.h>

// Pull the implementation directly so the native env compiles only this unit
// (the firmware src/ tree needs the Arduino toolchain and is not built here).
#include "../../src/scheduler/command_queue.cpp"

static ActuatorCommand mk(uint8_t id, CommandType type, uint32_t executeAt) {
  ActuatorCommand c;
  c.actuator_id = id;
  c.command_type = (uint8_t)type;
  c.value = 0;
  c.duration = 0;
  c.execute_at = executeAt;
  return c;
}

void setUp() {}
void tearDown() {}

// #4/#6: an immediately-due command inserted AFTER a far-future command must
// still come out first.
void test_immediate_not_blocked_by_future() {
  CommandQueue q;
  TEST_ASSERT_TRUE(q.push(mk(1, CommandType::POSITION, 1000000)));  // +1s
  TEST_ASSERT_TRUE(q.push(mk(2, CommandType::POSITION, 0)));        // now
  TEST_ASSERT_EQUAL_UINT16(2, q.count());

  ActuatorCommand out;
  TEST_ASSERT_TRUE(q.pop(out));
  TEST_ASSERT_EQUAL_UINT8(2, out.actuator_id);  // the immediate one first
  TEST_ASSERT_TRUE(q.pop(out));
  TEST_ASSERT_EQUAL_UINT8(1, out.actuator_id);
}

// #6: arbitrary insertion order -> ascending execute_at pop order.
void test_sorted_pop_order() {
  CommandQueue q;
  uint32_t times[] = {500, 100, 900, 300, 700};
  for (uint32_t t : times) TEST_ASSERT_TRUE(q.push(mk(0, CommandType::POSITION, t)));

  uint32_t prev = 0;
  ActuatorCommand out;
  while (q.pop(out)) {
    TEST_ASSERT_TRUE(out.execute_at >= prev);
    prev = out.execute_at;
  }
}

// hasDue reflects the earliest command, not insertion order.
void test_hasdue_uses_earliest() {
  CommandQueue q;
  q.push(mk(1, CommandType::POSITION, 2000));
  q.push(mk(2, CommandType::POSITION, 1000));
  TEST_ASSERT_FALSE(q.hasDue(999));
  TEST_ASSERT_TRUE(q.hasDue(1000));
  TEST_ASSERT_TRUE(q.hasDue(1500));
}

// FIFO stability among equal timestamps.
void test_fifo_among_equal_timestamps() {
  CommandQueue q;
  q.push(mk(10, CommandType::POSITION, 5000));
  q.push(mk(11, CommandType::POSITION, 5000));
  q.push(mk(12, CommandType::POSITION, 5000));
  ActuatorCommand out;
  q.pop(out); TEST_ASSERT_EQUAL_UINT8(10, out.actuator_id);
  q.pop(out); TEST_ASSERT_EQUAL_UINT8(11, out.actuator_id);
  q.pop(out); TEST_ASSERT_EQUAL_UINT8(12, out.actuator_id);
}

// #6: micros() wraparound — a command near UINT32_MAX must sort BEFORE one a
// little past the wrap (0x00000100), because it executes first.
void test_wraparound_ordering() {
  CommandQueue q;
  uint32_t nearMax = 0xFFFFFF00u;
  uint32_t pastWrap = 0x00000100u;  // = nearMax + 0x200 in wrap arithmetic
  q.push(mk(2, CommandType::POSITION, pastWrap));
  q.push(mk(1, CommandType::POSITION, nearMax));
  ActuatorCommand out;
  q.pop(out); TEST_ASSERT_EQUAL_UINT8(1, out.actuator_id);  // nearMax first
  q.pop(out); TEST_ASSERT_EQUAL_UINT8(2, out.actuator_id);

  // hasDue with a 'now' just past the wrap sees the nearMax command as due.
  CommandQueue q2;
  q2.push(mk(1, CommandType::POSITION, nearMax));
  TEST_ASSERT_TRUE(q2.hasDue(pastWrap));
}

// #7: pushPair inserts both, sorted, atomically.
void test_pushpair_inserts_both_sorted() {
  CommandQueue q;
  ActuatorCommand on = mk(5, CommandType::PULSE, 1000);
  ActuatorCommand off = mk(5, CommandType::OFF, 3000);
  TEST_ASSERT_TRUE(q.pushPair(on, off));
  TEST_ASSERT_EQUAL_UINT16(2, q.count());

  ActuatorCommand out;
  q.pop(out);
  TEST_ASSERT_EQUAL_UINT8((uint8_t)CommandType::PULSE, out.command_type);
  q.pop(out);
  TEST_ASSERT_EQUAL_UINT8((uint8_t)CommandType::OFF, out.command_type);
}

// #7: near-full pushPair must reject BOTH (never a lone ON).
void test_pushpair_rejects_atomically_when_full() {
  CommandQueue q;
  // Fill to SIZE-1: only one slot free, not enough for a pair.
  for (int i = 0; i < COMMAND_QUEUE_SIZE - 1; i++) {
    TEST_ASSERT_TRUE(q.push(mk(0, CommandType::POSITION, (uint32_t)i)));
  }
  uint16_t before = q.count();
  ActuatorCommand on = mk(9, CommandType::PULSE, 10);
  ActuatorCommand off = mk(9, CommandType::OFF, 20);
  TEST_ASSERT_FALSE(q.pushPair(on, off));
  TEST_ASSERT_EQUAL_UINT16(before, q.count());  // unchanged: nothing inserted
}

// A full queue rejects a normal command but still accepts a safety OFF
// (evicting the least-urgent entry).
void test_off_accepted_when_full() {
  CommandQueue q;
  for (int i = 0; i < COMMAND_QUEUE_SIZE; i++) {
    TEST_ASSERT_TRUE(q.push(mk(0, CommandType::POSITION, 1000 + (uint32_t)i)));
  }
  TEST_ASSERT_TRUE(q.isFull());
  // Normal command rejected.
  TEST_ASSERT_FALSE(q.push(mk(1, CommandType::POSITION, 500)));
  // OFF accepted (count stays at capacity, tail evicted).
  TEST_ASSERT_TRUE(q.push(mk(2, CommandType::OFF, 100)));
  TEST_ASSERT_EQUAL_UINT16(COMMAND_QUEUE_SIZE, q.count());
  // The OFF (earliest) is now at the head.
  ActuatorCommand out;
  q.peek(out);
  TEST_ASSERT_EQUAL_UINT8((uint8_t)CommandType::OFF, out.command_type);
}

// #2 (review): a new OFF must never evict an ALREADY-QUEUED OFF, or some other
// actuator would be left ON without its scheduled OFF.
void test_off_eviction_never_drops_existing_off() {
  CommandQueue q;
  // Fill with 64 PULSE + 64 OFF (interleaved times) => full at 128.
  for (int i = 0; i < 64; i++) {
    TEST_ASSERT_TRUE(q.push(mk((uint8_t)i, CommandType::PULSE, (uint32_t)(100 + i))));
  }
  for (int i = 0; i < 64; i++) {
    TEST_ASSERT_TRUE(q.push(mk((uint8_t)i, CommandType::OFF, (uint32_t)(10000 + i))));
  }
  TEST_ASSERT_TRUE(q.isFull());

  auto countOffs = [](CommandQueue& qq) {
    int offs = 0; ActuatorCommand c;
    while (qq.pop(c)) if ((CommandType)c.command_type == CommandType::OFF) offs++;
    return offs;
  };

  // Push one more OFF: it must displace a PULSE, not any of the 64 OFFs.
  TEST_ASSERT_TRUE(q.push(mk(200, CommandType::OFF, 5)));
  TEST_ASSERT_EQUAL_UINT16(COMMAND_QUEUE_SIZE, q.count());
  TEST_ASSERT_EQUAL_INT(65, countOffs(q));  // 64 original + 1 new, none lost
}

// If the queue is entirely OFFs, a new OFF is rejected rather than dropping one.
void test_off_rejected_when_all_offs() {
  CommandQueue q;
  for (int i = 0; i < COMMAND_QUEUE_SIZE; i++) {
    TEST_ASSERT_TRUE(q.push(mk((uint8_t)i, CommandType::OFF, (uint32_t)i)));
  }
  TEST_ASSERT_TRUE(q.isFull());
  TEST_ASSERT_FALSE(q.push(mk(200, CommandType::OFF, 500)));
  TEST_ASSERT_EQUAL_UINT16(COMMAND_QUEUE_SIZE, q.count());
}

void test_clear_empties() {
  CommandQueue q;
  q.push(mk(1, CommandType::POSITION, 10));
  q.push(mk(2, CommandType::POSITION, 20));
  q.clear();
  TEST_ASSERT_TRUE(q.isEmpty());
  TEST_ASSERT_EQUAL_UINT16(0, q.count());
  ActuatorCommand out;
  TEST_ASSERT_FALSE(q.pop(out));
}


// ---------------------------------------------------------------------------
// pushBatch: all-or-nothing for a whole percussion event
// ---------------------------------------------------------------------------
// A percussion can fire up to MAX_ACTIONS_PER_EVENT actions (strike + mute +
// servo position + second striker). Inserting them one at a time let a nearly
// full queue accept the strike and reject the mute, playing something the
// instrument never described.

void test_pushbatch_inserts_all_sorted() {
  CommandQueue q;
  ActuatorCommand batch[3] = {
    mk(1, CommandType::PULSE, 300),
    mk(2, CommandType::PULSE, 100),
    mk(3, CommandType::PULSE, 200),
  };
  TEST_ASSERT_TRUE(q.pushBatch(batch, 3));
  TEST_ASSERT_EQUAL_UINT16(3, q.count());

  ActuatorCommand out;
  q.pop(out); TEST_ASSERT_EQUAL_UINT8(2, out.actuator_id);   // 100
  q.pop(out); TEST_ASSERT_EQUAL_UINT8(3, out.actuator_id);   // 200
  q.pop(out); TEST_ASSERT_EQUAL_UINT8(1, out.actuator_id);   // 300
}

void test_pushbatch_rejects_atomically_when_short_on_space() {
  CommandQueue q;
  // Leave exactly 2 free slots, then ask for 3.
  for (uint16_t i = 0; i < COMMAND_QUEUE_SIZE - 2; i++) {
    TEST_ASSERT_TRUE(q.push(mk(9, CommandType::PULSE, 1000 + i)));
  }
  uint16_t before = q.count();

  ActuatorCommand batch[3] = {
    mk(1, CommandType::PULSE, 10),
    mk(2, CommandType::PULSE, 20),
    mk(3, CommandType::PULSE, 30),
  };
  TEST_ASSERT_FALSE(q.pushBatch(batch, 3));
  TEST_ASSERT_EQUAL_UINT16(before, q.count());   // rien insere

  // The head must still be the pre-existing command: no partial insertion.
  ActuatorCommand head;
  TEST_ASSERT_TRUE(q.peek(head));
  TEST_ASSERT_EQUAL_UINT8(9, head.actuator_id);

  // Two commands still fit, so a smaller batch is accepted.
  TEST_ASSERT_TRUE(q.pushBatch(batch, 2));
  TEST_ASSERT_EQUAL_UINT16(before + 2, q.count());
}

void test_pushbatch_exact_fit_accepted() {
  CommandQueue q;
  for (uint16_t i = 0; i < COMMAND_QUEUE_SIZE - 2; i++) {
    q.push(mk(9, CommandType::PULSE, 1000 + i));
  }
  ActuatorCommand batch[2] = {
    mk(1, CommandType::PULSE, 10),
    mk(2, CommandType::OFF, 20),
  };
  TEST_ASSERT_TRUE(q.pushBatch(batch, 2));
  TEST_ASSERT_TRUE(q.isFull());
}

void test_pushbatch_empty_is_a_noop_success() {
  CommandQueue q;
  TEST_ASSERT_TRUE(q.pushBatch(nullptr, 0));
  TEST_ASSERT_TRUE(q.isEmpty());
}

void test_pushbatch_refuses_oversized_batch() {
  CommandQueue q;
  ActuatorCommand batch[CommandQueue::MAX_BATCH + 1];
  for (uint8_t i = 0; i < CommandQueue::MAX_BATCH + 1; i++) {
    batch[i] = mk(i, CommandType::PULSE, 10 + i);
  }
  TEST_ASSERT_FALSE(q.pushBatch(batch, CommandQueue::MAX_BATCH + 1));
  TEST_ASSERT_TRUE(q.isEmpty());
}

// A batch is NOT given the OFF eviction privilege of push(): evicting several
// commands to make room for one event would trade one instrument's correctness
// for another's.
void test_pushbatch_with_off_does_not_evict() {
  CommandQueue q;
  for (uint16_t i = 0; i < COMMAND_QUEUE_SIZE; i++) {
    q.push(mk(9, CommandType::PULSE, 1000 + i));
  }
  ActuatorCommand batch[2] = {
    mk(1, CommandType::OFF, 10),
    mk(2, CommandType::OFF, 20),
  };
  TEST_ASSERT_FALSE(q.pushBatch(batch, 2));
  TEST_ASSERT_EQUAL_UINT16(COMMAND_QUEUE_SIZE, q.count());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_immediate_not_blocked_by_future);
  RUN_TEST(test_sorted_pop_order);
  RUN_TEST(test_hasdue_uses_earliest);
  RUN_TEST(test_fifo_among_equal_timestamps);
  RUN_TEST(test_wraparound_ordering);
  RUN_TEST(test_pushpair_inserts_both_sorted);
  RUN_TEST(test_pushpair_rejects_atomically_when_full);
  RUN_TEST(test_off_accepted_when_full);
  RUN_TEST(test_off_eviction_never_drops_existing_off);
  RUN_TEST(test_off_rejected_when_all_offs);
  RUN_TEST(test_clear_empties);
  RUN_TEST(test_pushbatch_inserts_all_sorted);
  RUN_TEST(test_pushbatch_rejects_atomically_when_short_on_space);
  RUN_TEST(test_pushbatch_exact_fit_accepted);
  RUN_TEST(test_pushbatch_empty_is_a_noop_success);
  RUN_TEST(test_pushbatch_refuses_oversized_batch);
  RUN_TEST(test_pushbatch_with_off_does_not_evict);
  return UNITY_END();
}
