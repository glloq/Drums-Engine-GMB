#include "pipeline_compiler.h"

static void parseActionAndCcBindings(const JsonObject& inst, CompiledPipeline& pipeline) {
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
    }
  }

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

bool PipelineCompiler::compileInstrument(const JsonObject& json, CompiledPipeline& pipeline) {
  pipeline.block_count = 0;
  pipeline.note_on_count = 0;
  pipeline.note_off_count = 0;
  pipeline.cc_binding_count = 0;
  pipeline.output_actuator_id = json["outputActuatorId"] | 0xFF;

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

  // Decoder le type de bloc
  if (strcmp(typeStr, "condition") == 0) {
    block.type = (uint8_t)BlockType::CONDITION;
  } else if (strcmp(typeStr, "transform") == 0) {
    block.type = (uint8_t)BlockType::TRANSFORM;
  } else if (strcmp(typeStr, "time") == 0) {
    block.type = (uint8_t)BlockType::TIME;
  } else if (strcmp(typeStr, "output") == 0) {
    block.type = (uint8_t)BlockType::OUTPUT;
  } else {
    return false;
  }

  block.subtype = json["subtype"] | 0;
  block.param1 = json["param1"] | 0;
  block.param2 = json["param2"] | 0;

  return true;
}

bool PipelineCompiler::compileAll(const JsonArray& instruments, PipelineLookup& lookup) {
  // Reinitialiser
  memset(lookup.note_to_pipeline, 0xFF, sizeof(lookup.note_to_pipeline));
  lookup.pipeline_count = 0;

  for (JsonObject inst : instruments) {
    if (lookup.pipeline_count >= MAX_PIPELINES) break;

    uint8_t midiNote = inst["midiNote"] | 0xFF;
    if (midiNote >= 128) continue;

    bool enabled = inst["enabled"] | true;
    if (!enabled) continue;

    CompiledPipeline& pipeline = lookup.pipelines[lookup.pipeline_count];
    pipeline.block_count = 0;
    pipeline.note_on_count = 0;
    pipeline.note_off_count = 0;
    pipeline.cc_binding_count = 0;

    // Verifier si un pipeline est defini
    JsonArray pipelineBlocks = inst["pipeline"].as<JsonArray>();
    if (pipelineBlocks.isNull()) {
      // Pas de pipeline defini, creer un pipeline par defaut
      // (sortie directe vers l'actionneur principal)
      JsonArray actuators = inst["actuators"].as<JsonArray>();
      if (!actuators.isNull() && actuators.size() > 0) {
        JsonObject firstAct = actuators[0].as<JsonObject>();
        pipeline.output_actuator_id = firstAct["id"] | 0;
      } else {
        pipeline.output_actuator_id = 0xFF;
      }
    } else {
      // Compiler le pipeline
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

    lookup.note_to_pipeline[midiNote] = lookup.pipeline_count;
    lookup.pipeline_count++;
  }

  DBGF("[Compiler] Compiled %d pipelines\n", lookup.pipeline_count);
  return true;
}

void PipelineCompiler::lookupToJson(const PipelineLookup& lookup, JsonObject& obj) {
  obj["pipeline_count"] = lookup.pipeline_count;

  JsonArray noteMap = obj["note_map"].to<JsonArray>();
  for (uint8_t i = 0; i < 128; i++) {
    if (lookup.note_to_pipeline[i] != 0xFF) {
      JsonObject entry = noteMap.add<JsonObject>();
      entry["note"] = i;
      entry["pipeline"] = lookup.note_to_pipeline[i];
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
  }
}
