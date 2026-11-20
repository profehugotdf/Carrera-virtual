/*
  correr_cinta_ble.ino — v7 (tandas)
  ESP32 que aprieta los botones de la cinta (optoacopladores PC817, a través del Nano traductor a 5 V).
  Lo usa la app Correr por Bluetooth (Web Bluetooth). Placa: ESP32 (núcleo Arduino-ESP32 3.x).

  Historia:
    v3: los comandos llegan como CARÁCTER de texto '1'..'4' (nRF Connect manda texto).
    v4: 300 ms de espera entre toques sueltos (contra el bombardeo de escrituras repetidas).
    v6: onWrite solo anota el pedido y loop() mueve los pines (condición de carrera entre los dos núcleos).
    v7: TANDAS (Hugo, 20-nov). La cinta sube 0,1 km/h por toque: de 6 a 15 son 90 toques, y mandados
        de a uno desde el teléfono (cola de 400 ms) tardaban 36 s. Ahora el ESP32 sabe a qué velocidad
        está la cinta y Correr le manda solo la velocidad final: el ESP32 da los toques él mismo, uno
        cada 300 ms (150 ms apretado + 150 ms suelto: probado por Hugo, preciso) → 90 toques en 27 s.
        Avisa por notificación la velocidad después de cada toque, así la app la muestra al mismo paso.
        Si se corta el Bluetooth en medio de una tanda, se detiene (no sigue apretando solo).

  Protocolo (característica RX, escritura, texto):
    '1' Vel+  '2' Vel-  '3' Pend+  '4' Pend-   → un toque suelto (como siempre)
    "S60"   → la cinta marca ahora 6,0 km/h (velocidad de referencia, en décimas)
    "V150"  → llevá la cinta a 15,0 km/h (necesita una referencia antes; si no, avisa "E")
    "X"     → frená la tanda donde está
    "?"     → responde "v7" (así Correr sabe que puede usar tandas)
  Notificaciones (característica TX): "v7", "C150" (velocidad actual en décimas), "E" (sin referencia).

  Pines (los de siempre): GPIO16 Vel+ (IN1), GPIO17 Vel- (IN2), GPIO18 Pend+ (IN3), GPIO19 Pend- (IN4).
  Mismos UUID que en Correr (index.html, CINTA_SERVICIO_UUID / CINTA_CARACTERISTICA_UUID / CINTA_TX_UUID).
*/
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define SERVICIO_UUID "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define RX_UUID       "6e400002-b5a3-f393-e0a9-e50e24dcca9e"   // la app escribe acá
#define TX_UUID       "6e400003-b5a3-f393-e0a9-e50e24dcca9e"   // el ESP32 avisa acá

const int PINES[5] = { -1, 16, 17, 18, 19 };   // índice = comando 1..4
const unsigned long TOQUE_MS = 150;            // toque suelto
const unsigned long ESPERA_SUELTO_MS = 300;    // entre toques sueltos (v4)
const unsigned long TANDA_ON_MS = 150;         // tanda: apretado
const unsigned long TANDA_OFF_MS = 150;        // tanda: suelto (la cinta cuenta el toque al soltar)

// Lo que anota onWrite (otro núcleo): loop() lo toma y lo limpia.
volatile int pedidoSuelto = 0;
volatile int pedidoRef = -1;        // décimas de km/h
volatile int pedidoDestino = -1;    // décimas de km/h
volatile bool pedidoFrenar = false, pedidoVersion = false;
volatile unsigned long ultimoSueltoMs = 0;

// Estado (solo lo toca loop())
int actual = -1;      // décimas de km/h que marca la cinta; -1 = no se sabe
int destino = -1;
bool apretado = false, enTanda = false;
int pinApretado = -1, sentidoTanda = 0;
unsigned long t0 = 0;

BLECharacteristic *tx = nullptr;
volatile bool conectado = false;

void avisar(const String &s) {
  if (!tx || !conectado) return;
  tx->setValue(s.c_str());
  tx->notify();
}

class ServidorCB : public BLEServerCallbacks {
  void onConnect(BLEServer *s) override { conectado = true; }
  void onDisconnect(BLEServer *s) override {
    conectado = false;
    pedidoFrenar = true;                 // sin teléfono, no se sigue apretando solo
    BLEDevice::startAdvertising();       // para poder reconectar
  }
};

class RxCB : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    String v = c->getValue();            // núcleo 3.x: String (no std::string)
    if (v.length() == 0) return;
    char c0 = v[0];
    if (c0 >= '1' && c0 <= '4' && v.length() == 1) {
      unsigned long ahora = millis();
      if (ahora - ultimoSueltoMs < ESPERA_SUELTO_MS) return;
      ultimoSueltoMs = ahora;
      pedidoSuelto = c0 - '0';
    } else if (c0 == 'S') { pedidoRef = v.substring(1).toInt(); }
    else if (c0 == 'V') { pedidoDestino = v.substring(1).toInt(); }
    else if (c0 == 'X') { pedidoFrenar = true; }
    else if (c0 == '?') { pedidoVersion = true; }
  }
};

void apretar(int pin) { digitalWrite(pin, HIGH); pinApretado = pin; apretado = true; t0 = millis(); }

void setup() {
  for (int i = 1; i <= 4; i++) { pinMode(PINES[i], OUTPUT); digitalWrite(PINES[i], LOW); }
  BLEDevice::init("Cinta Correr");
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new ServidorCB());
  BLEService *serv = server->createService(SERVICIO_UUID);
  BLECharacteristic *rx = serv->createCharacteristic(RX_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  rx->setCallbacks(new RxCB());
  tx = serv->createCharacteristic(TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  tx->addDescriptor(new BLE2902());
  serv->start();
  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICIO_UUID);
  BLEDevice::startAdvertising();
}

void loop() {
  unsigned long ahora = millis();

  // 1) pedidos que anotó onWrite
  if (pedidoVersion) { pedidoVersion = false; avisar("v7"); }
  if (pedidoFrenar) { pedidoFrenar = false; destino = actual; }
  if (pedidoRef >= 0) {
    int r = pedidoRef; pedidoRef = -1;
    if (!apretado) { actual = r; destino = r; avisar("C" + String(actual)); }
    else pedidoRef = r;                  // en medio de un toque: se toma al soltar
  }
  if (pedidoDestino >= 0) {
    int d = pedidoDestino; pedidoDestino = -1;
    if (actual < 0) avisar("E");         // sin referencia no se puede contar
    else destino = constrain(d, 0, 300);
  }

  // 2) soltar el toque en curso
  if (apretado) {
    if (ahora - t0 >= (enTanda ? TANDA_ON_MS : TOQUE_MS)) {
      digitalWrite(pinApretado, LOW);
      apretado = false; t0 = ahora;
      if (pinApretado == PINES[1] && actual >= 0) actual++;
      if (pinApretado == PINES[2] && actual > 0) actual--;
      if (pinApretado == PINES[1] || pinApretado == PINES[2]) avisar("C" + String(actual));
      if (!enTanda) destino = actual;    // un toque suelto mueve también el destino
    }
    return;
  }

  // 3) tanda: un toque más hacia el destino
  if (actual >= 0 && destino >= 0 && destino != actual) {
    if (ahora - t0 >= TANDA_OFF_MS) { enTanda = true; apretar(destino > actual ? PINES[1] : PINES[2]); }
    return;
  }
  enTanda = false;

  // 4) toque suelto
  if (pedidoSuelto) {
    int cmd = pedidoSuelto; pedidoSuelto = 0;
    if (cmd >= 1 && cmd <= 4) apretar(PINES[cmd]);
  }
}
