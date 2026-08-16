#include "pipeline_compiler.h"

static void parseActionAndCcBindings(const JsonObject& inst, CompiledPipeline& pipeline) {
  uint8_t maxGroup = 0;

  JsonArray noteOnActions = inst["noteOnActions"].as<JsonArray>();
  if (!noteOnActions.isNull()) {
    for (JsonObject action : noteOnActions) {
      if (pipeline.note_on_count >= MAX_ACTIONS_PER_EVENT) break;
      ActionStep& step = pipeline.note_on_actions[pipeline.note_on_count++];
      step.actuator_id = action["actuatorId"] | 0xFF;
      step.command_type = action["commandType"] | (uint8_t)CommandType::PULSE;
      step.value_source = action["valueSource"] | (uint8_t)ValueSource::VELOCITY;
      step.value_fixed = action["valueFixed"] | 0;
      step.cc_index = action["ccIndex"] | 0;
      step.delay_ms = action["delayMs"] | 0;
      step.duration_min_ms = action["durationMinMs"] | 0;
      step.duration_max_ms = action["durationMaxMs"] | 0;
      step.velocity_curve = action["velocityCurve"] | CURVE_LINEAR;
      step.alternate_group = action["alternateGroup"] | 0;
      if (step.alternate_group > maxGroup) maxGroup = step.alternate_group;
    }
  }

  JsonArray noteOffActions = inst["noteOffActions"].as<JsonArray>();
  if (!noteOffActions.isNull()) {
    for (JsonObject action : noteOffActions) {
      if (pipeline.note_off_count >= MAX_ACTIONS_PER_EVENT) break;
      ActionStep& step = pipeline.note_off_actions[pipeline.note_off_count++];
      step.actuator_id = action["actuatorId"] | 0xFF;
      step.command_type = action["commandType"] | (uint8_t)CommandType::OFF;
      step.value_source = action["valueSource"] | (uint8_t)ValueSource::FIXED;
      step.value_fixed = action["valueFixed"] | 0;
      step.cc_index = action["ccIndex"] | 0;
      step.delay_ms = action["delayMs"] | 0;
      step.duration_min_ms = action["durationMinMs"] | 0;
      step.duration_max_ms = action["durationMaxMs"] | 0;
      step.velocity_curve = action["velocityCurve"] | CURVE_LINEAR;
      step.alternate_group = action["alternateGroup"] | 0;
      if (step.alternate_group > maxGroup) maxGroup = step.alternate_group;
    }
  }

  pipeline.alternate_group_count = maxGroup;

  JsonArray ccBindings = inst["ccBindings"].as<JsonArray>();
  if (!ccBindings.isNull()) {
    for (JsonObject bindingJson : ccBindings) {
      if (pipeline.cc_binding_count >= MAX_CC_BINDINGS) break;
      CCBinding& binding = pipeline.cc_bindings[pipeline.cc_binding_count++];
      binding.cc_number = bindingJson["ccNumber"] | 0;
      binding.actuator_id = bindingJson["actuatorId"] | 0xFF;
      binding.command_type = bindingJson["commandType"] | (uint8_t)CommandType::POSITION;
      binding.range_min = bindingJson["rangeMin"] | 0;
      binding.range_max = bindingJson["rangeMax"] | 127;
      binding.curve = bindingJson["curve"] | CURVE_LINEAR;
      binding.inverted = bindingJson["inverted"] | false;
    }
  }
}

static void buildCcRoutingTable(PipelineLookup& lookup) {
  lookup.cc_route_count = 0;
  lookup.cc_route_dropped = 0;
  memset(lookup.cc_to_first, 0xFF, sizeof(lookup.cc_to_first));
  memset(lookup.cc_to_count, 0, sizeof(lookup.cc_to_count));

  for (uint8_t cc = 0; cc < 128; cc++) {
    uint8_t first = lookup.cc_route_count;
    uint8_t count = 0;

    for (uint8_t p = 0; p < lookup.pipeline_count; p++) {
      const CompiledPipeline& pipeline = lookup.pipelines[p];
      for (uint8_t i = 0; i < pipeline.cc_binding_count; i++) {
        const CCBinding& binding = pipeline.cc_bindings[i];
        if (binding.cc_number != cc || binding.actuator_id == 0xFF) continue;
        if (lookup.cc_route_count >= MAX_CC_ROUTES) {
          lookup.cc_route_dropped++;
          continue;
        }

        CCRoutingEntry& route = lookup.cc_routes[lookup.cc_route_count++];
        route.actuator_id = binding.actuator_id;
        route.command_type = binding.command_type;
        route.range_min = binding.range_min;
        route.range_max = binding.range_max;
        route.curve = binding.curve;
        route.channel = pipeline.midi_channel;   // 0 = OMNI
        route.inverted = binding.inverted;
        count++;
      }
    }

    if (count > 0) {
      lookup.cc_to_first[cc] = first;
      lookup.cc_to_count[cc] = count;
    }

  }
}

bool PipelineCompiler::compileInstrument(const JsonObject& json, CompiledPipeline& pipeline) {
  pipeline.block_count = 0;
  pipeline.note_on_count = 0;
  pipeline.note_off_count = 0;
  pipeline.cc_binding_count = 0;
  pipeline.retrigger_mode = json["retriggerMode"] | (uint8_t)RetriggerMode::IGNORE;
  pipeline.output_actuator_id = json["outputActuatorId"] | 0xFF;
  // Reset the routing key too: the caller may reuse a pipeline slot, and a
  // stale (channel, note) would survive into the next buildNoteMap().
  pipeline.midi_note = json["midiNote"] | 0xFF;
  pipeline.midi_channel = json["midiChannel"] | 10;

  JsonArray blocks = json["blocks"].as<JsonArray>();
  if (!blocks.isNull()) {
    for (JsonObject blockJson : blocks) {
      if (pipeline.block_count >= MAX_BLOCKS_PER_PIPELINE) break;

      CompiledBlock block;
      if (compileBlock(blockJson, block)) {
        pipeline.blocks[pipeline.block_count] = block;
        pipeline.block_count++;
      }
    }
  }

  parseActionAndCcBindings(json, pipeline);
  return (pipeline.block_count > 0) || (pipeline.note_on_count > 0) || (pipeline.note_off_count > 0);
}

bool PipelineCompiler::compileBlock(const JsonObject& json, CompiledBlock& block) {
  const char* typeStr = json["type"];
  if (!typeStr) return false;

  if (strcmp(typeStr, "condition") == 0) {
    block.type = (uint8_t)BlockType::CONDITION;
  } else if (strcmp(typeStr, "transform") == 0) {
    block.type = (uint8_t)BlockType::TRANSFORM;
  } else if (strcmp(typeStr, "time") == 0) {
    block.type = (uint8_t)BlockType::TIME;
  } else if (strcmp(typeStr, "output") == 0) {
    block.type = (uint8_t)BlockType::OUTPUT_BLOCK;
  } else {
    return false;
  }

  block.subtype = json["subtype"] | 0;
  block.param1 = json["param1"] | 0;
  block.param2 = json["param2"] | 0;

  return true;
}

bool PipelineCompiler::compileAll(const JsonArray& instruments, PipelineLookup& lookup) {
  lookup.clearNoteMap();
  lookup.pipeline_count = 0;
  lookup.cc_route_count = 0;
  lookup.cc_route_dropped = 0;
  memset(lookup.cc_to_first, 0xFF, sizeof(lookup.cc_to_first));
  memset(lookup.cc_to_count, 0, sizeof(lookup.cc_to_count));

  for (JsonObject inst : instruments) {
    if (lookup.pipeline_count >= MAX_PIPELINES) break;

    uint8_t midiNote = inst["midiNote"] | 0xFF;
    if (midiNote >= 128) continue;

    // 0 = OMNI, 1..16 = explicit channel. The default matches
    // InstrumentManager::instrumentFromJson() so both readers agree on what a
    // missing field means. Anything above 16 is a corrupted config; routing it
    // to every channel would be the more dangerous reading, so it is dropped.
    uint8_t midiChannel = inst["midiChannel"] | 10;
    if (midiChannel > MIDI_CHANNEL_COUNT) continue;

    bool enabled = inst["enabled"] | true;
    if (!enabled) continue;

    CompiledPipeline& pipeline = lookup.pipelines[lookup.pipeline_count];
    pipeline.block_count = 0;
    pipeline.note_on_count = 0;
    pipeline.note_off_count = 0;
    pipeline.cc_binding_count = 0;
    pipeline.midi_note = midiNote;
    pipeline.midi_channel = midiChannel;
    pipeline.retrigger_mode = inst["retriggerMode"] | (uint8_t)RetriggerMode::IGNORE;

    JsonArray pipelineBlocks = inst["pipeline"].as<JsonArray>();
    if (pipelineBlocks.isNull()) {
      pipeline.output_actuator_id = 0xFF;

      JsonArray actuators = inst["actuators"].as<JsonArray>();
      if (!actuators.isNull() && actuators.size() > 0) {
        JsonObject firstAct = actuators[0].as<JsonObject>();
        pipeline.output_actuator_id = firstAct["id"] | 0xFF;
      }

      JsonArray actuatorIds = inst["actuatorIds"].as<JsonArray>();
      if (pipeline.output_actuator_id == 0xFF && !actuatorIds.isNull() && actuatorIds.size() > 0) {
        pipeline.output_actuator_id = actuatorIds[0] | 0xFF;
      }
    } else {
      pipeline.output_actuator_id = inst["outputActuatorId"] | 0;
      for (JsonObject blockJson : pipelineBlocks) {
        if (pipeline.block_count >= MAX_BLOCKS_PER_PIPELINE) break;

        CompiledBlock block;
        if (compileBlock(blockJson, block)) {
          pipeline.blocks[pipeline.block_count] = block;
          pipeline.block_count++;
        }
      }
    }

    parseActionAndCcBindings(inst, pipeline);

    lookup.pipeline_count++;
  }

  buildNoteMap(lookup);
  buildCcRoutingTable(lookup);

  DBGF("[Compiler] Compiled %d pipelines, %d CC routes (%d dropped)\n", lookup.pipeline_count, lookup.cc_route_count, lookup.cc_route_dropped);
  return true;
}

// Fill the (channel, note) -> pipeline table. The precedence rules live in
// core/midi_routing.h and are shared with InstrumentManager, so both routing
// paths can never drift apart.
void PipelineCompiler::buildNoteMap(PipelineLookup& lookup) {
  uint16_t shadowed = midiBuildNoteMap<uint8_t>(
      lookup.note_to_pipeline, 0xFF, lookup.pipeline_count,
      [&lookup](uint16_t i) -> MidiBinding {
        const CompiledPipeline& pipe = lookup.pipelines[i];
        return { pipe.midi_channel, pipe.midi_note, pipe.midi_note < 128 };
      });

  if (shadowed > 0) {
    DBGF("[Compiler] %u pipeline(s) shadowed by an earlier (channel, note) mapping\n",
         shadowed);
  }
}

void PipelineCompiler::lookupToJson(const PipelineLookup& lookup, JsonObject& obj) {
  obj["pipeline_count"] = lookup.pipeline_count;
  obj["cc_route_count"] = lookup.cc_route_count;
  obj["cc_route_dropped"] = lookup.cc_route_dropped;

  // The note map is now 16 x 128. Emitting every populated cell would repeat an
  // OMNI instrument sixteen times, so report it per pipeline instead: one entry
  // per pipeline with the channel it answers on (0 = OMNI / all channels).
  JsonArray noteMap = obj["note_map"].to<JsonArray>();
  for (uint8_t p = 0; p < lookup.pipeline_count; p++) {
    const CompiledPipeline& pipe = lookup.pipelines[p];
    if (pipe.midi_note >= 128) continue;
    JsonObject entry = noteMap.add<JsonObject>();
    entry["note"] = pipe.midi_note;
    entry["channel"] = pipe.midi_channel;
    entry["pipeline"] = p;
    // A pipeline can be compiled but shadowed by a duplicate (channel, note).
    entry["routed"] = (lookup.pipelineFor(
        pipe.midi_channel == 0 ? 1 : pipe.midi_channel, pipe.midi_note) == p);
  }

  JsonArray ccMap = obj["cc_map"].to<JsonArray>();
  for (uint8_t cc = 0; cc < 128; cc++) {
    if (lookup.cc_to_first[cc] != 0xFF) {
      JsonObject entry = ccMap.add<JsonObject>();
      entry["cc"] = cc;
      entry["first"] = lookup.cc_to_first[cc];
      entry["count"] = lookup.cc_to_count[cc];
    }
  }

  JsonArray pipelines = obj["pipelines"].to<JsonArray>();
  for (uint8_t i = 0; i < lookup.pipeline_count; i++) {
    const CompiledPipeline& p = lookup.pipelines[i];
    JsonObject pObj = pipelines.add<JsonObject>();
    pObj["blocks"] = p.block_count;
    pObj["output_id"] = p.output_actuator_id;
    pObj["note_on_count"] = p.note_on_count;
    pObj["note_off_count"] = p.note_off_count;
    pObj["cc_binding_count"] = p.cc_binding_count;
    pObj["retrigger_mode"] = p.retrigger_mode;
  }
}
