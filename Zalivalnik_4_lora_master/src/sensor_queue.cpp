#include "sensor_queue.h"
#include "Lora_master.h"
#include "unified_lora_handler.h"

// --- Statična alokacija (brez malloc!) ---
SensorOperation sensorOpsQueue[SENSOR_QUEUE_SIZE];
uint8_t sensor_queue_head = 0;
uint8_t sensor_queue_tail = 0;
uint8_t sensor_queue_count = 0;
SensorOperation *current_sensor_op = nullptr;

// Prototipi lokalnih funkcij
static void execute_sensor_operation(SensorOperation *op);
static void reset_operation(SensorOperation *op);

//------------------------------------------------------------------------------------------------------------------------
void Sensor_InitQueue()
{
  sensor_queue_head = 0;
  sensor_queue_tail = 0;
  sensor_queue_count = 0;
  current_sensor_op = nullptr;

  // Inicializiraj vse operacije
  for (int i = 0; i < SENSOR_QUEUE_SIZE; i++)
  {
    reset_operation(&sensorOpsQueue[i]);
  }

  Serial.println("[SENSOR_QUEUE] Inicializirana.");
}

//------------------------------------------------------------------------------
// Funkcija za intervalno branje senzorjev
bool Sensor_IntervalRead(uint32_t interval_ms)
{
  // NOVO: Sinhronizacija na cele minute
  static bool firstReadDone = false;
  static unsigned long lastSensorRead = 0; // Čas zadnjega branja senzorjev
  bool readTriggered = false;

  if (!firstReadDone)
  {
    // PRVI KLIC: Počakaj na prehod minute (sekunda == 0)
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    if (timeinfo.tm_sec == 0 && interval_ms > 0)
    {
      // Točno ob prehodu minute (XX:XX:00)
      Serial.println("[SENSORS] Prvi sinhroniziran klic ob prehodu minute.");

      lastSensorRead = millis();
      firstReadDone = true;
      readTriggered = true;
      // Dodaj operacije v čakalno vrsto
    }
  }
  else
  {
    // NASLEDNJI KLICI: Normalen interval
    if (interval_ms > 0 && (millis() - lastSensorRead >= interval_ms))
    {
      lastSensorRead = millis();

      Serial.println("[SENSORS] Periodično branje senzorjev.");
      readTriggered = true;
      // Dodaj operacije v čakalno vrsto

    }
  }

  return readTriggered;
}

//------------------------------------------------------------------------------------------------------------------------
static void reset_operation(SensorOperation *op)
{
  op->state = SensorTaskState::IDLE;
  op->retry_count = 0;
  op->last_attempt_time = 0;
}

//------------------------------------------------------------------------------------------------------------------------
bool Sensor_QueueOperation(SensorTaskType type)
{
  if (sensor_queue_count >= SENSOR_QUEUE_SIZE)
  {
    Serial.println("[SENSOR_QUEUE] POLNA! Preskakujem operacijo.");
    return false;
  }

  SensorOperation *op = &sensorOpsQueue[sensor_queue_tail];
  op->type = type;
  op->state = SensorTaskState::IDLE;
  op->retry_count = 0;
  op->last_attempt_time = 0;

  // Določi kateri LoRa ukaz poslati
  switch (type)
  {
  case SensorTaskType::READ_SENSORS:
    op->lora_command = CommandType::CMD_GET_SENSORS;
    break;
  case SensorTaskType::READ_INA:
    op->lora_command = CommandType::CMD_GET_INA_DATA;
    break;
  case SensorTaskType::READ_POWER:
    op->lora_command = CommandType::CMD_GET_POWER;
    break;
  case SensorTaskType::READ_WATER_CONSUMPTION:
    op->lora_command = CommandType::CMD_GET_WATER_CONSUMPTION;
    break;
  default:
    return false;
  }

  sensor_queue_tail = (sensor_queue_tail + 1) % SENSOR_QUEUE_SIZE;
  sensor_queue_count++;

  Serial.printf("[SENSOR_QUEUE] Dodana operacija tipa %d. V vrsti: %d/%d\n",
                (int)type, sensor_queue_count, SENSOR_QUEUE_SIZE);
  return true;
}

//------------------------------------------------------------------------------------------------------------------------
static void execute_sensor_operation(SensorOperation *op)
{
  const char *type_names[] = {"SENSORS", "INA", "POWER", "WATER"};
  Serial.printf("[SENSOR_QUEUE] Izvajam: %s (poskus %d/%d)\n",
                type_names[(int)op->type],
                op->retry_count,
                SENSOR_RETRY_MAX);

  op->state = SensorTaskState::SENDING;
  op->last_attempt_time = millis();

  // Nastavi kontekst na SENSOR_QUEUE (onemogočimo LoRa retry)
  lora_set_context(LoRaContext::SENSOR_QUEUE);

  // Pošlji LoRa ukaz
  Lora_prepare_and_send_packet(op->lora_command, nullptr, 0);

  // Prestavimo v stanje čakanja
  op->state = SensorTaskState::WAITING_LORA;
}

//------------------------------------------------------------------------------------------------------------------------
void Sensor_ProcessQueue()
{  
  static unsigned long lastSensorCheck = 0;

  // Procesiranje senzorske čakalne vrste 10x na sekundo
  if (millis() - lastSensorCheck < 100)
  {
    return; 
  }
  lastSensorCheck = millis();

  // Če imamo aktivno operacijo, najprej obdelaj njo
  if (current_sensor_op != nullptr)
  {
    SensorOperation *op = current_sensor_op;

    switch (op->state)
    {
    case SensorTaskState::WAITING_LORA:
      // Preveri timeout za LoRa odgovor
      if (millis() - op->last_attempt_time > SENSOR_LORA_TIMEOUT_MS)
      {
        Serial.printf("[SENSOR_QUEUE] LoRa timeout pri poskusu %d/%d\n",
                      op->retry_count, SENSOR_RETRY_MAX);
        op->retry_count++;              
        op->state = SensorTaskState::RETRY;
      }
      break;

    case SensorTaskState::WAITING_FIREBASE:
      // Timeout se preveri v Sensor_OnFirebaseResponse
      break;

    case SensorTaskState::RETRY:
      

      if (op->retry_count >= SENSOR_RETRY_MAX)
      {
        Serial.println("[SENSOR_QUEUE] NAPAKA: Preseženo število poskusov!");
        op->state = SensorTaskState::FAILED;
      }
      else
      {
        // Počakaj pred ponovnim poskusom
        if (millis() - op->last_attempt_time >= SENSOR_RETRY_DELAY_MS)
        {
          Serial.println("[SENSOR_QUEUE] Ponovni poskus...");
          execute_sensor_operation(op);
        }
      }
      break;

    case SensorTaskState::SUCCESS:
    case SensorTaskState::FAILED:
      // Operacija končana, sprosti trenutno operacijo
      Serial.printf("[SENSOR_QUEUE] Operacija %s.\n",
                    op->state == SensorTaskState::SUCCESS ? "USPEŠNA" : "NEUSPEŠNA");

      reset_operation(op);
      current_sensor_op = nullptr;

      // Premakni head naprej
      sensor_queue_head = (sensor_queue_head + 1) % SENSOR_QUEUE_SIZE;
      sensor_queue_count--;
      break;

    default:
      break;
    }

    return; // Ne procesiramo naslednje, dokler trenutna ni končana
  }

  // Če ni aktivne operacije in je v vrsti kaj, začni naslednjo
  if (sensor_queue_count > 0 && current_sensor_op == nullptr)
  {
    current_sensor_op = &sensorOpsQueue[sensor_queue_head];
    execute_sensor_operation(current_sensor_op);
  }
}

//------------------------------------------------------------------------------------------------------------------------
// Pokliči to v Lora_handle_received_packet, ko prideš do RESPONSE_SENSORS, RESPONSE_INA_DATA itd.
void Sensor_OnLoRaResponse(bool success)
{
  if (current_sensor_op == nullptr)
  {
    return; // Ni aktivne operacije
  }

  if (current_sensor_op->state != SensorTaskState::WAITING_LORA)
  {
    return; // Nismo v pravem stanju
  }

  if (success)
  {
    Serial.println("[SENSOR_QUEUE] LoRa odgovor prejet. Čakam na Firebase...");
    current_sensor_op->state = SensorTaskState::WAITING_FIREBASE;
    current_sensor_op->last_attempt_time = millis(); // Reset timer za Firebase timeout
  }
  else
  {
    Serial.println("[SENSOR_QUEUE] LoRa odgovor NAPAKA.");
    current_sensor_op->state = SensorTaskState::RETRY;
  }
}

//------------------------------------------------------------------------------------------------------------------------
// Pokliči to v Firebase_processResponse pri "updateSensorTask" in "updateINA3221Task"
void Sensor_OnFirebaseResponse(bool success)
{
  if (current_sensor_op == nullptr)
  {
    return;
  }

  if (current_sensor_op->state != SensorTaskState::WAITING_FIREBASE)
  {
    return;
  }

  if (success)
  {
    Serial.println("[SENSOR_QUEUE] Firebase uspešno posodobljen.");
    current_sensor_op->state = SensorTaskState::SUCCESS;
  }
  else
  {
    Serial.println("[SENSOR_QUEUE] Firebase NAPAKA.");
    current_sensor_op->state = SensorTaskState::RETRY;
  }
}

//------------------------------------------------------------------------------------------------------------------------
// Vrne število aktivnih operacij (v vrsti + trenutna)
uint8_t Sensor_GetActiveOperationCount()
{
  return sensor_queue_count + (current_sensor_op != nullptr ? 1 : 0);
}