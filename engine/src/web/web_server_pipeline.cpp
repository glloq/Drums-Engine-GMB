#include "web_server.h"

// ============================================================================
// Pipeline Editor API Handlers
// ============================================================================

// GET /api/pipeline/block-schema
// Returns the full schema of available block types, subtypes, and parameters
// Used by the Pipeline Editor UI to render dynamic block configuration forms
void WebServerManager::_handleGetPipelineBlockSchema(AsyncWebServerRequest* req) {
  JsonDocument doc;
  JsonArray blockTypes = doc.to<JsonArray>();

  // --- CONDITION blocks ---
  {
    JsonObject cond = blockTypes.add<JsonObject>();
    cond["type"] = "condition";
    cond["typeId"] = (uint8_t)BlockType::CONDITION;
    cond["label"] = "Condition";
    cond["description"] = "Filtre/gate: bloque le pipeline si la condition echoue";
    cond["color"] = "#e94560";
    JsonArray subtypes = cond["subtypes"].to<JsonArray>();

    { JsonObject s = subtypes.add<JsonObject>();
      s["subtype"] = COND_THRESHOLD;
      s["id"] = "threshold";
      s["label"] = "Seuil";
      s["description"] = "Compare une variable ou la valeur courante a un seuil";
      JsonArray params = s["params"].to<JsonArray>();
      { JsonObject p = params.add<JsonObject>(); p["name"] = "param1"; p["label"] = "Variable (0x7F=valeur courante, bit7=inferieur)"; p["type"] = "uint8"; p["default"] = 0x7F; p["min"] = 0; p["max"] = 255; }
      { JsonObject p = params.add<JsonObject>(); p["name"] = "param2"; p["label"] = "Seuil"; p["type"] = "uint16"; p["default"] = 64; p["min"] = 0; p["max"] = 127; }
    }
    { JsonObject s = subtypes.add<JsonObject>();
      s["subtype"] = COND_ROUND_ROBIN;
      s["id"] = "round_robin";
      s["label"] = "Round-Robin";
      s["description"] = "Alternance entre N groupes (passe si c'est le tour du groupe cible)";
      JsonArray params = s["params"].to<JsonArray>();
      { JsonObject p = params.add<JsonObject>(); p["name"] = "param1"; p["label"] = "Groupe cible (0-based)"; p["type"] = "uint8"; p["default"] = 0; p["min"] = 0; p["max"] = 7; }
      { JsonObject p = params.add<JsonObject>(); p["name"] = "param2"; p["label"] = "Nombre de groupes"; p["type"] = "uint16"; p["default"] = 2; p["min"] = 2; p["max"] = 8; }
    }
    { JsonObject s = subtypes.add<JsonObject>();
      s["subtype"] = COND_VELOCITY_SPLIT;
      s["id"] = "velocity_split";
      s["label"] = "Velocity Split";
      s["description"] = "Passe si la velocite est dans la plage [min, max]";
      JsonArray params = s["params"].to<JsonArray>();
      { JsonObject p = params.add<JsonObject>(); p["name"] = "param1"; p["label"] = "Velocity min"; p["type"] = "uint8"; p["default"] = 0; p["min"] = 0; p["max"] = 127; }
      { JsonObject p = params.add<JsonObject>(); p["name"] = "param2"; p["label"] = "Velocity max"; p["type"] = "uint16"; p["default"] = 127; p["min"] = 0; p["max"] = 127; }
    }
    { JsonObject s = subtypes.add<JsonObject>();
      s["subtype"] = COND_RANDOM;
      s["id"] = "random";
      s["label"] = "Aleatoire";
      s["description"] = "Passe avec une probabilite de N% (0-100)";
      JsonArray params = s["params"].to<JsonArray>();
      { JsonObject p = params.add<JsonObject>(); p["name"] = "param1"; p["label"] = "(reserve)"; p["type"] = "uint8"; p["default"] = 0; p["min"] = 0; p["max"] = 0; }
      { JsonObject p = params.add<JsonObject>(); p["name"] = "param2"; p["label"] = "Probabilite (%)"; p["type"] = "uint16"; p["default"] = 50; p["min"] = 0; p["max"] = 100; }
    }
    { JsonObject s = subtypes.add<JsonObject>();
      s["subtype"] = COND_NOTE_RANGE;
      s["id"] = "note_range";
      s["label"] = "Note Range";
      s["description"] = "Passe si la note MIDI source est dans la plage [min, max]";
      JsonArray params = s["params"].to<JsonArray>();
      { JsonObject p = params.add<JsonObject>(); p["name"] = "param1"; p["label"] = "Note min (0-127)"; p["type"] = "uint8"; p["default"] = 0; p["min"] = 0; p["max"] = 127; }
      { JsonObject p = params.add<JsonObject>(); p["name"] = "param2"; p["label"] = "Note max (0-127)"; p["type"] = "uint16"; p["default"] = 127; p["min"] = 0; p["max"] = 127; }
    }
  }

  // --- TRANSFORM blocks ---
  {
    JsonObject trans = blockTypes.add<JsonObject>();
    trans["type"] = "transform";
    trans["typeId"] = (uint8_t)BlockType::TRANSFORM;
    trans["label"] = "Transform";
    trans["description"] = "Transforme la valeur qui traverse le pipeline";
    trans["color"] = "#533483";
    JsonArray subtypes = trans["subtypes"].to<JsonArray>();

    { JsonObject s = subtypes.add<JsonObject>();
      s["subtype"] = TRANS_VELOCITY_CURVE;
      s["id"] = "velocity_curve";
      s["label"] = "Courbe Velocite";
      s["description"] = "Applique une courbe (lineaire, expo, log) a la valeur";
      JsonArray params = s["params"].to<JsonArray>();
      { JsonObject p = params.add<JsonObject>(); p["name"] = "param1"; p["label"] = "Courbe"; p["type"] = "enum";
        JsonArray opts = p["options"].to<JsonArray>();
        { JsonObject o = opts.add<JsonObject>(); o["value"] = CURVE_LINEAR; o["label"] = "Lineaire"; }
        { JsonObject o = opts.add<JsonObject>(); o["value"] = CURVE_EXPO; o["label"] = "Exponentiel"; }
        { JsonObject o = opts.add<JsonObject>(); o["value"] = CURVE_LOG; o["label"] = "Logarithmique"; }
      }
      { JsonObject p = params.add<JsonObject>(); p["name"] = "param2"; p["label"] = "(reserve)"; p["type"] = "uint16"; p["default"] = 0; p["min"] = 0; p["max"] = 0; }
    }
    { JsonObject s = subtypes.add<JsonObject>();
      s["subtype"] = TRANS_GAIN;
      s["id"] = "gain";
      s["label"] = "Gain";
      s["description"] = "Multiplie la valeur (100=1x, 200=2x, 50=0.5x)";
      JsonArray params = s["params"].to<JsonArray>();
      { JsonObject p = params.add<JsonObject>(); p["name"] = "param1"; p["label"] = "(reserve)"; p["type"] = "uint8"; p["default"] = 0; p["min"] = 0; p["max"] = 0; }
      { JsonObject p = params.add<JsonObject>(); p["name"] = "param2"; p["label"] = "Gain (%)"; p["type"] = "uint16"; p["default"] = 100; p["min"] = 10; p["max"] = 400; }
    }
    { JsonObject s = subtypes.add<JsonObject>();
      s["subtype"] = TRANS_CLAMP;
      s["id"] = "clamp";
      s["label"] = "Clamp";
      s["description"] = "Limite la valeur entre min et max";
      JsonArray params = s["params"].to<JsonArray>();
      { JsonObject p = params.add<JsonObject>(); p["name"] = "param1"; p["label"] = "Min"; p["type"] = "uint8"; p["default"] = 0; p["min"] = 0; p["max"] = 127; }
      { JsonObject p = params.add<JsonObject>(); p["name"] = "param2"; p["label"] = "Max"; p["type"] = "uint16"; p["default"] = 127; p["min"] = 0; p["max"] = 127; }
    }
    { JsonObject s = subtypes.add<JsonObject>();
      s["subtype"] = TRANS_INVERT;
      s["id"] = "invert";
      s["label"] = "Inverser";
      s["description"] = "Inverse la valeur (127 - value)";
      JsonArray params = s["params"].to<JsonArray>();
      { JsonObject p = params.add<JsonObject>(); p["name"] = "param1"; p["label"] = "(reserve)"; p["type"] = "uint8"; p["default"] = 0; p["min"] = 0; p["max"] = 0; }
      { JsonObject p = params.add<JsonObject>(); p["name"] = "param2"; p["label"] = "(reserve)"; p["type"] = "uint16"; p["default"] = 0; p["min"] = 0; p["max"] = 0; }
    }
    { JsonObject s = subtypes.add<JsonObject>();
      s["subtype"] = TRANS_SET_VAR;
      s["id"] = "set_var";
      s["label"] = "Stocker Variable";
      s["description"] = "Stocke la valeur courante dans une variable globale (pass-through)";
      JsonArray params = s["params"].to<JsonArray>();
      { JsonObject p = params.add<JsonObject>(); p["name"] = "param1"; p["label"] = "Index variable (0-127)"; p["type"] = "uint8"; p["default"] = 0; p["min"] = 0; p["max"] = 127; }
      { JsonObject p = params.add<JsonObject>(); p["name"] = "param2"; p["label"] = "(reserve)"; p["type"] = "uint16"; p["default"] = 0; p["min"] = 0; p["max"] = 0; }
    }
  }

  // --- TIME blocks ---
  {
    JsonObject time = blockTypes.add<JsonObject>();
    time["type"] = "time";
    time["typeId"] = (uint8_t)BlockType::TIME;
    time["label"] = "Temps";
    time["description"] = "Controle temporel: delai, duree d'impulsion, rampe";
    time["color"] = "#4CAF50";
    JsonArray subtypes = time["subtypes"].to<JsonArray>();

    { JsonObject s = subtypes.add<JsonObject>();
      s["subtype"] = TIME_PULSE;
      s["id"] = "pulse";
      s["label"] = "Impulsion";
      s["description"] = "Mappe la velocite vers une duree d'impulsion [min, max] ms (termine le pipeline)";
      s["terminal"] = true;
      JsonArray params = s["params"].to<JsonArray>();
      { JsonObject p = params.add<JsonObject>(); p["name"] = "param1"; p["label"] = "Duree min (ms, 0=fixe)"; p["type"] = "uint8"; p["default"] = 5; p["min"] = 0; p["max"] = 255; }
      { JsonObject p = params.add<JsonObject>(); p["name"] = "param2"; p["label"] = "Duree max (ms)"; p["type"] = "uint16"; p["default"] = 30; p["min"] = 1; p["max"] = 5000; }
    }
    { JsonObject s = subtypes.add<JsonObject>();
      s["subtype"] = TIME_DELAY;
      s["id"] = "delay";
      s["label"] = "Delai";
      s["description"] = "Retarde la sortie de N ms (cumulable)";
      JsonArray params = s["params"].to<JsonArray>();
      { JsonObject p = params.add<JsonObject>(); p["name"] = "param1"; p["label"] = "(reserve)"; p["type"] = "uint8"; p["default"] = 0; p["min"] = 0; p["max"] = 0; }
      { JsonObject p = params.add<JsonObject>(); p["name"] = "param2"; p["label"] = "Delai (ms)"; p["type"] = "uint16"; p["default"] = 10; p["min"] = 1; p["max"] = 5000; }
    }
    { JsonObject s = subtypes.add<JsonObject>();
      s["subtype"] = TIME_RAMP;
      s["id"] = "ramp";
      s["label"] = "Rampe";
      s["description"] = "Genere N micro-steps de 0 a la valeur sur une duree (termine le pipeline)";
      s["terminal"] = true;
      JsonArray params = s["params"].to<JsonArray>();
      { JsonObject p = params.add<JsonObject>(); p["name"] = "param1"; p["label"] = "Nombre de steps (0=auto)"; p["type"] = "uint8"; p["default"] = 0; p["min"] = 0; p["max"] = 20; }
      { JsonObject p = params.add<JsonObject>(); p["name"] = "param2"; p["label"] = "Duree totale (ms)"; p["type"] = "uint16"; p["default"] = 100; p["min"] = 10; p["max"] = 5000; }
    }
    { JsonObject s = subtypes.add<JsonObject>();
      s["subtype"] = TIME_GATE;
      s["id"] = "gate";
      s["label"] = "Gate";
      s["description"] = "Bloque si le dernier declenchement etait il y a moins de N ms";
      JsonArray params = s["params"].to<JsonArray>();
      { JsonObject p = params.add<JsonObject>(); p["name"] = "param1"; p["label"] = "(reserve)"; p["type"] = "uint8"; p["default"] = 0; p["min"] = 0; p["max"] = 0; }
      { JsonObject p = params.add<JsonObject>(); p["name"] = "param2"; p["label"] = "Intervalle min (ms)"; p["type"] = "uint16"; p["default"] = 50; p["min"] = 1; p["max"] = 10000; }
    }
  }

  // --- OUTPUT blocks ---
  {
    JsonObject out = blockTypes.add<JsonObject>();
    out["type"] = "output";
    out["typeId"] = (uint8_t)BlockType::OUTPUT_BLOCK;
    out["label"] = "Sortie";
    out["description"] = "Genere une commande vers un actionneur (termine le pipeline)";
    out["color"] = "#FF9800";
    JsonArray subtypes = out["subtypes"].to<JsonArray>();

    { JsonObject s = subtypes.add<JsonObject>();
      s["subtype"] = OUT_PULSE;
      s["id"] = "pulse";
      s["label"] = "Impulsion";
      s["description"] = "Envoie une commande PULSE a l'actionneur";
      s["terminal"] = true;
      JsonArray params = s["params"].to<JsonArray>();
      { JsonObject p = params.add<JsonObject>(); p["name"] = "param1"; p["label"] = "Actionneur ID"; p["type"] = "actuator_id"; p["default"] = 0; }
      { JsonObject p = params.add<JsonObject>(); p["name"] = "param2"; p["label"] = "Duree (unite cmd)"; p["type"] = "uint16"; p["default"] = 0; p["min"] = 0; p["max"] = 65535; }
    }
    { JsonObject s = subtypes.add<JsonObject>();
      s["subtype"] = OUT_POSITION;
      s["id"] = "position";
      s["label"] = "Position";
      s["description"] = "Envoie une commande POSITION a l'actionneur";
      s["terminal"] = true;
      JsonArray params = s["params"].to<JsonArray>();
      { JsonObject p = params.add<JsonObject>(); p["name"] = "param1"; p["label"] = "Actionneur ID"; p["type"] = "actuator_id"; p["default"] = 0; }
      { JsonObject p = params.add<JsonObject>(); p["name"] = "param2"; p["label"] = "(reserve)"; p["type"] = "uint16"; p["default"] = 0; p["min"] = 0; p["max"] = 0; }
    }
    { JsonObject s = subtypes.add<JsonObject>();
      s["subtype"] = OUT_PWM;
      s["id"] = "pwm";
      s["label"] = "PWM";
      s["description"] = "Envoie une commande PWM a l'actionneur";
      s["terminal"] = true;
      JsonArray params = s["params"].to<JsonArray>();
      { JsonObject p = params.add<JsonObject>(); p["name"] = "param1"; p["label"] = "Actionneur ID"; p["type"] = "actuator_id"; p["default"] = 0; }
      { JsonObject p = params.add<JsonObject>(); p["name"] = "param2"; p["label"] = "(reserve)"; p["type"] = "uint16"; p["default"] = 0; p["min"] = 0; p["max"] = 0; }
    }
    { JsonObject s = subtypes.add<JsonObject>();
      s["subtype"] = OUT_OFF;
      s["id"] = "off";
      s["label"] = "Arret";
      s["description"] = "Envoie une commande OFF a l'actionneur";
      s["terminal"] = true;
      JsonArray params = s["params"].to<JsonArray>();
      { JsonObject p = params.add<JsonObject>(); p["name"] = "param1"; p["label"] = "Actionneur ID"; p["type"] = "actuator_id"; p["default"] = 0; }
      { JsonObject p = params.add<JsonObject>(); p["name"] = "param2"; p["label"] = "(reserve)"; p["type"] = "uint16"; p["default"] = 0; p["min"] = 0; p["max"] = 0; }
    }
  }

  _sendJson(req, 200, doc);
}

// GET /api/pipeline/blocks?instrumentId=<id>
// Returns the pipeline blocks for a specific instrument
void WebServerManager::_handleGetPipelineBlocks(AsyncWebServerRequest* req) {
  uint8_t instrId = _extractId(req, "instrumentId");
  const InstrumentConfig* inst = _instrMgr->getInstrument(instrId);
  if (!inst) {
    _sendError(req, 404, "Instrument not found");
    return;
  }

  // Find the compiled pipeline for this instrument. With chained note→pipeline
  // (multi-channel support), walk the list and match the instrument's MIDI channel.
  const PipelineLookup& lookup = _eventProc->getLookup();
  uint8_t pipelineIdx = lookup.note_to_pipeline[inst->midiNote];
  while (pipelineIdx != 0xFF && pipelineIdx < lookup.pipeline_count) {
    const CompiledPipeline& p = lookup.pipelines[pipelineIdx];
    uint8_t pchan = p.midi_channel;
    uint8_t ichan = (inst->midiChannel > 16) ? 0 : inst->midiChannel;
    if (pchan == ichan) break;
    pipelineIdx = p.next_for_note;
  }

  JsonDocument doc;
  doc["instrumentId"] = instrId;
  doc["midiNote"] = inst->midiNote;

  if (pipelineIdx != 0xFF && pipelineIdx < _eventProc->getLookup().pipeline_count) {
    const CompiledPipeline& pipeline = _eventProc->getLookup().pipelines[pipelineIdx];
    doc["pipelineIndex"] = pipelineIdx;
    doc["blockCount"] = pipeline.block_count;
    doc["outputActuatorId"] = pipeline.output_actuator_id;
    doc["retriggerMode"] = pipeline.retrigger_mode;

    JsonArray blocks = doc["blocks"].to<JsonArray>();
    for (uint8_t i = 0; i < pipeline.block_count; i++) {
      const CompiledBlock& b = pipeline.blocks[i];
      JsonObject bObj = blocks.add<JsonObject>();

      // Map type to string
      switch ((BlockType)b.type) {
        case BlockType::CONDITION:   bObj["type"] = "condition"; break;
        case BlockType::TRANSFORM:   bObj["type"] = "transform"; break;
        case BlockType::TIME:        bObj["type"] = "time"; break;
        case BlockType::OUTPUT_BLOCK: bObj["type"] = "output"; break;
      }
      bObj["typeId"] = b.type;
      bObj["subtype"] = b.subtype;
      bObj["param1"] = b.param1;
      bObj["param2"] = b.param2;
    }

    // Also include the actions and CC bindings
    JsonArray noteOn = doc["noteOnActions"].to<JsonArray>();
    for (uint8_t i = 0; i < pipeline.note_on_count; i++) {
      const ActionStep& a = pipeline.note_on_actions[i];
      JsonObject aObj = noteOn.add<JsonObject>();
      aObj["actuatorId"] = a.actuator_id;
      aObj["commandType"] = a.command_type;
      aObj["valueSource"] = a.value_source;
      aObj["valueFixed"] = a.value_fixed;
      aObj["ccIndex"] = a.cc_index;
      aObj["delayMs"] = a.delay_ms;
      aObj["durationMinMs"] = a.duration_min_ms;
      aObj["durationMaxMs"] = a.duration_max_ms;
      aObj["velocityCurve"] = a.velocity_curve;
      aObj["alternateGroup"] = a.alternate_group;
    }

    JsonArray noteOff = doc["noteOffActions"].to<JsonArray>();
    for (uint8_t i = 0; i < pipeline.note_off_count; i++) {
      const ActionStep& a = pipeline.note_off_actions[i];
      JsonObject aObj = noteOff.add<JsonObject>();
      aObj["actuatorId"] = a.actuator_id;
      aObj["commandType"] = a.command_type;
      aObj["valueSource"] = a.value_source;
      aObj["valueFixed"] = a.value_fixed;
      aObj["ccIndex"] = a.cc_index;
      aObj["delayMs"] = a.delay_ms;
      aObj["durationMinMs"] = a.duration_min_ms;
      aObj["durationMaxMs"] = a.duration_max_ms;
      aObj["velocityCurve"] = a.velocity_curve;
      aObj["alternateGroup"] = a.alternate_group;
    }

    JsonArray ccBinds = doc["ccBindings"].to<JsonArray>();
    for (uint8_t i = 0; i < pipeline.cc_binding_count; i++) {
      const CCBinding& b = pipeline.cc_bindings[i];
      JsonObject bObj = ccBinds.add<JsonObject>();
      bObj["ccNumber"] = b.cc_number;
      bObj["actuatorId"] = b.actuator_id;
      bObj["commandType"] = b.command_type;
      bObj["rangeMin"] = b.range_min;
      bObj["rangeMax"] = b.range_max;
      bObj["curve"] = b.curve;
      bObj["inverted"] = b.inverted;
    }
  } else {
    doc["pipelineIndex"] = nullptr;
    doc["blockCount"] = 0;
    doc["blocks"].to<JsonArray>();
    doc["noteOnActions"].to<JsonArray>();
    doc["noteOffActions"].to<JsonArray>();
    doc["ccBindings"].to<JsonArray>();
  }

  _sendJson(req, 200, doc);
}

// PUT /api/pipeline/blocks?instrumentId=<id>
// Updates the pipeline blocks for a specific instrument
// Body: { "blocks": [...], "outputActuatorId": N, "noteOnActions": [...], "noteOffActions": [...], "ccBindings": [...], "retriggerMode": N }
void WebServerManager::_handleSetPipelineBlocks(AsyncWebServerRequest* req, uint8_t* data, size_t len) {
  uint8_t instrId = _extractId(req, "instrumentId");
  InstrumentConfig* inst = _instrMgr->getInstrument(instrId);
  if (!inst) {
    _sendError(req, 404, "Instrument not found");
    return;
  }

  JsonDocument doc;
  if (deserializeJson(doc, (char*)data, len)) {
    _sendError(req, 400, "Invalid JSON");
    return;
  }

  // Build an instrument JSON with the pipeline blocks for the compiler
  JsonDocument instrDoc;
  JsonObject instrJson = instrDoc.to<JsonObject>();
  instrJson["midiNote"] = inst->midiNote;
  instrJson["enabled"] = inst->enabled;
  instrJson["retriggerMode"] = doc["retriggerMode"] | (uint8_t)inst->retriggerMode;
  instrJson["outputActuatorId"] = doc["outputActuatorId"] | 0xFF;

  // Copy pipeline blocks array
  JsonArray srcBlocks = doc["blocks"].as<JsonArray>();
  if (!srcBlocks.isNull() && srcBlocks.size() > 0) {
    JsonArray pipeline = instrJson["pipeline"].to<JsonArray>();
    for (JsonObject srcBlock : srcBlocks) {
      JsonObject b = pipeline.add<JsonObject>();
      b["type"] = srcBlock["type"];
      b["subtype"] = srcBlock["subtype"];
      b["param1"] = srcBlock["param1"];
      b["param2"] = srcBlock["param2"];
    }
  }

  // Copy actions and bindings
  JsonArray srcNoteOn = doc["noteOnActions"].as<JsonArray>();
  if (!srcNoteOn.isNull()) {
    instrJson["noteOnActions"] = srcNoteOn;
  }
  JsonArray srcNoteOff = doc["noteOffActions"].as<JsonArray>();
  if (!srcNoteOff.isNull()) {
    instrJson["noteOffActions"] = srcNoteOff;
  }
  JsonArray srcCc = doc["ccBindings"].as<JsonArray>();
  if (!srcCc.isNull()) {
    instrJson["ccBindings"] = srcCc;
  }

  // Update the instrument's retrigger mode
  if (doc.containsKey("retriggerMode")) {
    inst->retriggerMode = doc["retriggerMode"] | 0;
  }

  // Update the instrument's actions in-memory
  inst->noteOnCount = 0;
  if (!srcNoteOn.isNull()) {
    for (JsonObject action : srcNoteOn) {
      if (inst->noteOnCount >= MAX_ACTIONS_PER_EVENT) break;
      ActionStep& step = inst->noteOnActions[inst->noteOnCount++];
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
    }
  }
  inst->noteOffCount = 0;
  if (!srcNoteOff.isNull()) {
    for (JsonObject action : srcNoteOff) {
      if (inst->noteOffCount >= MAX_ACTIONS_PER_EVENT) break;
      ActionStep& step = inst->noteOffActions[inst->noteOffCount++];
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
    }
  }
  inst->ccBindingCount = 0;
  if (!srcCc.isNull()) {
    for (JsonObject binding : srcCc) {
      if (inst->ccBindingCount >= MAX_CC_BINDINGS) break;
      CCBinding& b = inst->ccBindings[inst->ccBindingCount++];
      b.cc_number = binding["ccNumber"] | 0;
      b.actuator_id = binding["actuatorId"] | 0xFF;
      b.command_type = binding["commandType"] | (uint8_t)CommandType::POSITION;
      b.range_min = binding["rangeMin"] | 0;
      b.range_max = binding["rangeMax"] | 127;
      b.curve = binding["curve"] | CURVE_LINEAR;
      b.inverted = binding["inverted"] | false;
    }
  }

  // Save instruments and recompile
  _storage->saveInstruments(*_instrMgr);
  _recompileLookupFromInstruments();

  JsonDocument resp;
  resp["ok"] = true;
  resp["message"] = "Pipeline updated and recompiled";
  _sendJson(req, 200, resp);
}

// POST /api/pipeline/test
// Test-fire a pipeline by simulating a NoteOn event
// Body: { "instrumentId": N, "velocity": N }
void WebServerManager::_handleTestPipeline(AsyncWebServerRequest* req, uint8_t* data, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, (char*)data, len)) {
    _sendError(req, 400, "Invalid JSON");
    return;
  }

  uint8_t instrId = doc["instrumentId"] | 0xFF;
  uint8_t velocity = doc["velocity"] | 80;

  const InstrumentConfig* inst = _instrMgr->getInstrument(instrId);
  if (!inst) {
    _sendError(req, 404, "Instrument not found");
    return;
  }

  // Create a synthetic NoteOn event
  MidiEvent ev;
  ev.type = MIDI_EVT_NOTE_ON;
  ev.channel = inst->midiChannel;
  ev.data1 = inst->midiNote;
  ev.data2 = velocity;
  ev.timestamp = micros();

  _eventProc->processMidiEvent(ev);

  JsonDocument resp;
  resp["ok"] = true;
  resp["note"] = inst->midiNote;
  resp["velocity"] = velocity;
  _sendJson(req, 200, resp);
}
