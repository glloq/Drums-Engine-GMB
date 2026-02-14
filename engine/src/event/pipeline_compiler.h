#ifndef PIPELINE_COMPILER_H
#define PIPELINE_COMPILER_H

#include <ArduinoJson.h>
#include "../core/types.h"
#include "../core/config.h"

// ============================================================================
// PipelineCompiler - Compile JSON UI -> Tables statiques
// ============================================================================
// Convertit la configuration JSON (cree par l'interface web)
// en tables CompiledPipeline optimisees pour le temps reel.
// UI Graph -> CompiledPipeline[] -> Tables statiques
// ============================================================================

class PipelineCompiler {
public:
  // Compiler un JSON d'instrument en pipeline
  static bool compileInstrument(const JsonObject& json, CompiledPipeline& pipeline);

  // Compiler un bloc JSON en CompiledBlock
  static bool compileBlock(const JsonObject& json, CompiledBlock& block);

  // Compiler toute la configuration et remplir la PipelineLookup
  static bool compileAll(const JsonArray& instruments, PipelineLookup& lookup);

  // Serialiser une PipelineLookup en JSON (pour debug/sauvegarde)
  static void lookupToJson(const PipelineLookup& lookup, JsonObject& obj);
};

#endif // PIPELINE_COMPILER_H
