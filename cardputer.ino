/**
Modified Text messager for M5stack Cardputer. 
20 second pause while LLM compute boots
Parses Json via uart serial for cardputer functionality
 */

#include "M5Cardputer.h"
#include "M5GFX.h"
#include <ArduinoJson.h>

// Define the hardware serial port for the LLM (pins G2: RX, G1: TX).
HardwareSerial LLMSerial(1);

M5Canvas canvas(&M5Cardputer.Display);
String inputData = "";   // This will hold user input (after boot, set to "> ")
String llmWorkId = "llm_work_id";  // Will be updated after setup.

bool setupComplete = false; // Indicates whether the LLM setup query was successful.
bool bootComplete = false;  // Indicates whether the initial 20-second boot delay has elapsed.
unsigned long bootStart = 0;  // Time when boot delay started.

/**
 * @brief Reads from LLMSerial until a line is received that begins with '{'
 *        and successfully parses as JSON.
 *
 * @param timeout Maximum time in milliseconds to wait for a valid JSON message.
 * @return A String containing the valid JSON message, or an empty string if timeout.
 */
String readJsonMessage(unsigned long timeout = 5000) {
  String candidate = "";
  unsigned long startTime = millis();
  while (millis() - startTime < timeout) {
    if (LLMSerial.available() > 0) {
      candidate = LLMSerial.readStringUntil('\n');
      candidate.trim();
      int idx = candidate.indexOf('{');
      if (idx >= 0) {
        candidate = candidate.substring(idx);
        // Try parsing the candidate as JSON.
        StaticJsonDocument<256> doc;
        DeserializationError err = deserializeJson(doc, candidate);
        if (!err) {
          return candidate;
        }
      }
    }
    delay(10);
  }
  return "";
}

/**
 * @brief Sends the setup JSON query and parses the response.
 *
 * @return true if setup was successful (work_id obtained), false otherwise.
 */
bool sendSetupQuery() {
  // Build the JSON setup query using ArduinoJson.
  StaticJsonDocument<512> doc;
  doc["request_id"] = "llm_001";
  doc["work_id"] = "llm";
  doc["action"] = "setup";
  doc["object"] = "llm.setup";
  JsonObject dataObj = doc.createNestedObject("data");
  dataObj["model"] = "qwen2.5-0.5B-prefill-20e";
  dataObj["response_format"] = "llm.utf-8.stream";
  dataObj["input"] = "llm.utf-8.stream";
  dataObj["enoutput"] = true;
  dataObj["max_token_len"] = 1023;
  dataObj["prompt"] = "You are a knowledgeable assistant capable of answering various questions and providing information.";

  String jsonString;
  serializeJson(doc, jsonString);
  Serial.print("Sending setup JSON query to LLM: ");
  Serial.println(jsonString);
  LLMSerial.println(jsonString);

  delay(200); // Increased delay to allow the LLM to respond.
  
  // Read only a valid JSON message.
  String response = readJsonMessage(5000);
  response.trim();
  Serial.print("Raw setup response: ");
  Serial.println(response);

  // Parse the setup response.
  StaticJsonDocument<512> respDoc;
  DeserializationError err = deserializeJson(respDoc, response);
  if (err) {
    Serial.print("Error parsing setup response: ");
    Serial.println(err.c_str());
    return false;
  }
  // Verify the request_id.
  const char* respRequestId = respDoc["request_id"];
  if (strcmp(respRequestId, "llm_001") != 0) {
    Serial.println("Request ID mismatch in setup response.");
    return false;
  }
  // Check for an error in the response.
  JsonObject errorObj = respDoc["error"];
  if (errorObj.containsKey("code") && errorObj["code"].as<int>() != 0) {
    Serial.print("Error in setup response: ");
    Serial.println(errorObj["message"].as<const char*>());
    return false;
  }
  // Get the work_id from the response.
  const char* workId = respDoc["work_id"];
  if (workId == nullptr) {
    Serial.println("No work_id in setup response.");
    return false;
  }
  llmWorkId = String(workId);
  return true;
}

/**
 * @brief Reads inference response messages in a streaming manner.
 *
 * It reads line-by-line until a message with "finish": true is received or timeout.
 *
 * @param timeout Maximum time to wait for the complete response.
 * @return The concatenated delta text.
 */
String readInferenceResponse(unsigned long timeout = 5000) {
  String completeResponse = "";
  bool finished = false;
  unsigned long startTime = millis();
  
  while (!finished && (millis() - startTime < timeout)) {
    if (LLMSerial.available() > 0) {
      // Read a line terminated by '\n'
      String line = LLMSerial.readStringUntil('\n');
      line.trim();
      if (line.length() > 0) {
        Serial.print("Raw line: ");
        Serial.println(line);
        // Parse the line as JSON.
        StaticJsonDocument<256> doc;
        DeserializationError error = deserializeJson(doc, line);
        if (error) {
          Serial.print("Error parsing line: ");
          Serial.println(error.c_str());
          continue;
        }
        JsonObject dataObj = doc["data"];
        if (dataObj.containsKey("delta")) {
          const char* delta = dataObj["delta"];
          completeResponse += String(delta);
          // Display each delta piece as it arrives.
          canvas.println(delta);
          canvas.pushSprite(4, 4);
          Serial.print("Parsed delta: ");
          Serial.println(delta);
        }
        // Check if finish flag is true.
        if (dataObj.containsKey("finish") && dataObj["finish"].as<bool>() == true) {
          finished = true;
        }
      }
    }
    delay(10);
  }
  return completeResponse;
}

void setup() {
  // Initialize USB Serial for debugging.
  Serial.begin(115200);
  Serial.println("Debug over USB started");

  // Initialize LLMSerial on G2 (RX) and G1 (TX) at 115200 baud.
  LLMSerial.begin(115200, SERIAL_8N1, G2, G1);
  Serial.println("LLMSerial started on pins G2 (RX) and G1 (TX)");

  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);

  // Set up the display.
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setTextSize(0.5);
  M5Cardputer.Display.drawRect(0, 0, M5Cardputer.Display.width(),
                               M5Cardputer.Display.height() - 28, GREEN);
  M5Cardputer.Display.setTextFont(&fonts::FreeSerifBoldItalic18pt7b);
  M5Cardputer.Display.fillRect(0, M5Cardputer.Display.height() - 4,
                               M5Cardputer.Display.width(), 4, GREEN);

  // Set up the canvas (sprite) for scrolling text.
  canvas.setTextFont(&fonts::FreeSerifBoldItalic18pt7b);
  canvas.setTextSize(0.5);
  canvas.createSprite(M5Cardputer.Display.width() - 8,
                      M5Cardputer.Display.height() - 36);
  canvas.setTextScroll(true);

  // Display boot delay message.
  canvas.println("Waiting for LLM to boot... (20s)");
  canvas.pushSprite(4, 4);

  // Record the boot start time.
  bootStart = millis();
}

void loop() {
  // During boot delay, block user input.
  if (!bootComplete) {
    if (millis() - bootStart >= 20000) { // 20 seconds elapsed.
      bootComplete = true;
      // Send the setup query.
      if (sendSetupQuery()) {
        setupComplete = true;
        inputData = "> ";  // Unlock input.
        canvas.println("LLM boot complete. Input enabled.");
        canvas.println("WorkID: " + llmWorkId);
        canvas.pushSprite(4, 4);
        Serial.println("LLM boot complete. Input enabled.");
        Serial.print("WorkID: ");
        Serial.println(llmWorkId);
      } else {
        canvas.println("LLM setup failed. Check configuration.");
        canvas.pushSprite(4, 4);
        Serial.println("LLM setup failed. Check configuration.");
        // Optionally, allow input even if setup failed.
        inputData = "> ";
      }
    } else {
      return; // Still waiting; do nothing.
    }
  }

  M5Cardputer.update();
  if (M5Cardputer.Keyboard.isChange()) {
    if (M5Cardputer.Keyboard.isPressed()) {
      Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();

      // Append new characters from the keyboard.
      for (auto i : status.word) {
        inputData += i;
      }

      // Process delete key (preserve the prompt "> ").
      if (status.del && inputData.length() > 2) {
        inputData.remove(inputData.length() - 1);
      }

      // When Enter is pressed, build and send the inference query.
      if (status.enter) {
        // Remove the prompt ("> ") from the input.
        String userInput = inputData.substring(2);

        // Build the inference JSON query.
        StaticJsonDocument<256> doc;
        doc["request_id"] = "llm_001";
        doc["work_id"] = llmWorkId; // Use the work_id obtained during setup.
        doc["action"] = "inference";
        doc["object"] = "llm.utf-8.stream";
        JsonObject jsonData = doc.createNestedObject("data");
        jsonData["delta"] = userInput;
        jsonData["index"] = 0;
        jsonData["finish"] = true;

        String jsonString;
        serializeJson(doc, jsonString);

        // Debug output over USB.
        Serial.print("Sending inference JSON query to LLM: ");
        Serial.println(jsonString);

        // Send the inference query over LLMSerial.
        LLMSerial.println(jsonString);

        // Instead of reading one lump sum, read JSON lines until "finish": true is received.
        String completeResponse = readInferenceResponse(5000);
        Serial.print("Full response: ");
        Serial.println(completeResponse);
        canvas.println("Full response:");
        canvas.println(completeResponse);
        canvas.pushSprite(4, 4);

        // Reset the input prompt.
        inputData = "> ";
      }

      // Clear and update the input area on the display.
      M5Cardputer.Display.fillRect(0, M5Cardputer.Display.height() - 28,
                                   M5Cardputer.Display.width(), 25, BLACK);
      M5Cardputer.Display.drawString(inputData, 4, M5Cardputer.Display.height() - 24);
    }
  }
}
