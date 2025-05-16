void loop() {
  timeClient.update();
  delay(2000);

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, skipping this cycle.");
    return;
  }
