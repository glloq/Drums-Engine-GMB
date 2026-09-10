#ifndef GMB_SYSEX_SERVICE_H
#define GMB_SYSEX_SERVICE_H

#include "gmb_capabilities.h"
#include "gmb_descriptor.h"
#include "gmb_sysex.h"

// ============================================================================
// GmbSysExService — requetes, cache, revision, notifications
// ============================================================================
// Le service est le seul objet a garder de l'etat. Il ne connait ni le
// transport, ni LittleFS, ni le serveur web :
//   - handleSysEx() prend un message COMPLET et rend les octets a renvoyer ;
//   - takeNotification() rend la trame 0x11 quand il y en a une a pousser ;
//   - la persistance passe par persistPending()/markPersisted(), a l'appelant
//     de choisir OU et QUAND ecrire.
// C'est ce decouplage qui le rend testable en natif et transport-agnostique :
// USB, BLE, DIN ou rtpMIDI branchent le meme service.
//
// Contraintes temps reel
// ----------------------
// handleSysEx() est appelable depuis le contexte de reception MIDI (coeur temps
// reel) : il ne fait qu'analyser 6 a 8 octets et recopier un segment deja
// calcule. Aucune generation de JSON, aucune allocation, aucun acces disque, et
// aucun effet de bord sur les actionneurs, les notes actives ou le scheduler.
// La (re)construction du descripteur, elle, se fait dans rebuild(), a
// l'activation d'une configuration — jamais sur un chemin de frappe.
// ============================================================================

// FNV-1a 32 bits. Expose pour que les tests verifient la detection de
// changement sur les memes octets que le service.
uint32_t gmbHashBytes(const void* data, size_t len);

class GmbSysExService {
public:
  struct Diagnostics {
    uint32_t handshakeRequests = 0;
    uint32_t chunkRequests = 0;
    uint32_t invalidPackets = 0;      // trames GMB mal formees
    uint32_t rateLimited = 0;
    uint32_t outOfRangeChunks = 0;
    uint32_t notificationsSent = 0;
    uint32_t httpDescriptorReads = 0;
    uint32_t rebuilds = 0;
    uint32_t lastRequestMs = 0;       // 0 = jamais
    uint32_t lastTransferMs = 0;      // dernier segment servi
    uint32_t lastNotificationMs = 0;
    uint32_t lastRebuildMs = 0;
  };

  // Limiteur de debit du plan de controle. Un transfert complet de descripteur
  // demande une quinzaine de requetes en rafale, ce qui est legitime ; au-dela
  // d'un debit soutenu, les requetes sont abandonnees sans reponse.
  // N'intervient JAMAIS sur les NoteOn/NoteOff/CC : ce sont deux chemins
  // distincts, ce limiteur ne voit que du SysEx GMB.
  static constexpr uint16_t RATE_BURST = 40;
  static constexpr uint16_t RATE_PER_SEC = 20;

  // `revision` et `stateHash` viennent du stockage (0/0 au premier demarrage).
  void begin(uint32_t instanceId, uint32_t revision, uint32_t stateHash);

  // Reconstruire l'instantane de capacites et le descripteur en cache depuis la
  // configuration ACTIVE. A appeler a l'activation d'une configuration validee,
  // et uniquement la.
  //
  // La revision n'augmente que si le descripteur effectif a REELLEMENT change :
  // un renommage ou un reglage d'interface qui ne modifie aucune capacite
  // produit exactement les memes octets, donc pas d'increment et pas de
  // notification. Renvoie true si les capacites ont change.
  bool rebuild(const GmbBuildInputs& in, uint32_t nowMs);

  // Traiter un message SysEx complet (F0..F7). Renvoie le nombre d'octets a
  // emettre, 0 s'il n'y a rien a repondre.
  size_t handleSysEx(const uint8_t* msg, size_t len,
                     uint8_t* out, size_t outCap, uint32_t nowMs);

  // Recuperer la notification bloc 0x11 en attente, s'il y en a une.
  size_t takeNotification(uint8_t* out, size_t cap, uint32_t nowMs);
  bool notificationPending() const { return _notifyPending; }

  // Persistance de la revision (appelant : Core 0, hors verrou temps reel).
  bool persistPending() const { return _persistPending; }
  void markPersisted() { _persistPending = false; }

  // --- Etat expose (diagnostique, endpoint HTTP, handshake) ---
  const char* descriptor() const { return _descriptor; }
  size_t descriptorSize() const { return _descriptorLen; }
  uint16_t totalChunks() const;
  uint32_t revision() const { return _revision; }
  uint32_t stateHash() const { return _stateHash; }
  uint32_t instanceId() const { return _instanceId; }
  uint8_t flags() const;
  GmbDetail detail() const { return _detail; }
  uint8_t instrumentsDropped() const { return _instrumentsDropped; }
  const CapabilitySnapshot& snapshot() const { return _snapshot; }
  const Diagnostics& diagnostics() const { return _diag; }

  // Le serveur web signale que GET /gmb/descriptor.json est servi : c'est ce
  // qui arme le bit 0 de `flags` dans le handshake. Ne jamais l'annoncer sans
  // que la route existe reellement.
  void setHttpAvailable(bool available) { _httpAvailable = available; }
  void noteHttpRead(uint32_t nowMs) { _diag.httpDescriptorReads++; _diag.lastRequestMs = nowMs; }

  // Taille du plus grand message que le service peut produire.
  static constexpr size_t MAX_RESPONSE = GMB_CHUNK_FRAME_MAX;

private:
  bool _allow(uint32_t nowMs);
  uint32_t _timingHash() const;

  CapabilitySnapshot _snapshot;
  // Capacite utile : GMB_DESCRIPTOR_MAX - 1. Le dernier octet porte toujours un
  // terminateur, pour que descriptor() soit une chaine C valide.
  char _descriptor[GMB_DESCRIPTOR_MAX];
  size_t _descriptorLen = 0;
  GmbDetail _detail = GmbDetail::FULL;
  uint8_t _instrumentsDropped = 0;

  uint32_t _instanceId = 0;
  uint32_t _revision = 0;
  uint32_t _stateHash = 0;
  uint32_t _lastTimingHash = 0;

  bool _built = false;
  bool _httpAvailable = false;
  bool _persistPending = false;
  bool _notifyPending = false;
  uint8_t _notifyFlags = 0;

  uint16_t _tokens = RATE_BURST;
  uint32_t _lastRefillMs = 0;

  Diagnostics _diag;
};

#endif // GMB_SYSEX_SERVICE_H
