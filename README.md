# Tri_oled_Dash
A real-time desk telemetry monitor powered by an ESP32 and three 0.96" OLED screens (via a TCA9548A multiplexer) that displays live Spotify playback, PC performance stats over UDP, and an auto-syncing clock.


An ESP32-powered workspace telemetry monitor featuring a triple 0.96" OLED display setup driven by a TCA9548A I2C multiplexer to seamlessly bypass I2C address conflicts across multiple SSD1306 screens.

The embedded firmware communicates directly with a background Python host application running on the PC, which gathers real-time system performance metrics—including CPU load, RAM utilization, and GPU temperatures—and streams them to the microcontroller over low-latency local UDP sockets. Simultaneously, the system queries the Spotify Web API to pull live playback telemetry, displaying active track titles, artist details, and playback state in real time. The module also incorporates an auto-synchronizing real-time clock (RTC) that fetches and updates the local time and date over Wi-Fi on boot, providing a complete hardware-software bridge for custom desk monitoring.

THERE IS SOME CONFIGURATIONS TO DO, PLEASE READ INSTRUCTIONS.TXT
