# MINE-ENVIRONMENT-MONITORING-AND-SAFETY-ROVER
🚧 IoT-based mine environment monitoring rover built on ESP32. Detects air-quality gases, methane and carbon monoxide, measures temperature and humidity, avoids obstacles, streams live video (ESP32-CAM), and sends data to a ThingsBoard dashboard.
🚧 Mine Environment Monitoring Rover
An IoT-based rover that enters mines and other enclosed spaces ahead of people to detect harmful gases, avoid obstacles, stream live video, and report everything to an online dashboard in real time.

📌 Problem Statement
Underground mines and enclosed spaces can contain harmful gases such as carbon monoxide, methane and smoke, which cannot be seen or smelled. Sending workers or rescue teams in to check conditions puts their lives at risk. This project provides a low-cost machine that goes in first, measures the environment, avoids obstacles, shows the path ahead, and reports to people outside.
✨ Features
Air-quality detection (ammonia, CO₂, smoke, benzene) with MQ-135
Methane detection with MQ-4
Carbon monoxide detection with MQ-7
Temperature and humidity measurement with DHT11
Automatic obstacle avoidance with HC-SR04 ultrasonic sensor (stop, beep, turn)
Live video of the path ahead with ESP32-CAM
Buzzer alerts: short beeps for obstacles, continuous tone for gas alerts
Real-time ThingsBoard dashboard over Wi-Fi
🧰 Hardware Required
S.No	Component	Purpose
1	ESP32	Main controller
2	MQ-135 sensor	Air quality (ammonia, CO₂, smoke, benzene)
3	MQ-4 sensor	Methane detection
4	MQ-7 sensor	Carbon monoxide detection
5	DHT11 sensor	Temperature and humidity
6	Ultrasonic sensor (HC-SR04)	Obstacle detection
7	L298N motor driver	Motor control
8	4WD car chassis with DC motors	Movement
9	ESP32-CAM	Live video of path ahead
10	Buzzer	Audible alerts
11	Battery pack, switch and jumper wires	Power and connections
Software / platform: Arduino IDE, ThingsBoard (online dashboard)
🏗️ System Overview

The ESP32 reads the gas, temperature, humidity and distance sensors, drives the four motors through the L298N, switches the buzzer, and publishes all readings over Wi-Fi to ThingsBoard. The ESP32-CAM runs separately on its own supply and streams video.
🔌 Circuit Diagram

ESP32 pin connections
Device	Connection	ESP32 pin
MQ-135 (analog out)	Analog input	GPIO 34
MQ-4 (analog out)	Analog input	GPIO 35
MQ-7 (analog out)	Analog input	GPIO 32
DHT11 (data)	Digital input	GPIO 26
Ultrasonic TRIG / ECHO	Digital out / in	GPIO 25 / 13
Buzzer	Digital output	GPIO 4
L298N IN1 to IN4	Direction control	GPIO 18, 19, 21, 22
L298N ENA, ENB	Speed (PWM)	GPIO 5, 23
> All grounds (battery, L298N, ESP32 and sensors) must be connected together. The three MQ sensors need a 5 V supply.
ESP32-CAM wiring

⚙️ How It Works
Obstacle avoidance: the HC-SR04 measures distance (Distance = Time × Speed of sound / 2). Below the set limit, the rover stops, beeps and turns away.
Gas monitoring: the MQ sensors give analog voltages that the ESP32 compares with safe limits. If a limit is crossed, the buzzer sounds continuously and the dashboard shows an alert.
IoT dashboard: all readings are sent over Wi-Fi to ThingsBoard as live values and trend graphs.
Live video: the ESP32-CAM streams video that can be opened in a browser or on the dashboard.

   ```
Assemble the hardware using the circuit diagram and pin table above.
Open the code in the Arduino IDE and install the ESP32 board package.
Install the libraries your code uses (for example the DHT sensor library and a ThingsBoard/MQTT client library).
Copy `firmware/rover\_esp32/secrets.h.example` to `secrets.h` and enter your Wi-Fi name, password and ThingsBoard device token.
Upload the rover code to the ESP32, and the camera code to the ESP32-CAM (an external USB-serial programmer is needed).
Power the rover, open your ThingsBoard dashboard, and open the camera stream in a browser.
📊 Results
Test	Expected result
Clean air	Low sensor values, no alarm
Gas near sensor	Value rises, buzzer on, dashboard alert
Obstacle ahead	Rover stops, beeps, turns
Temperature / humidity	Values shown on dashboard

Camera	Live video visible
![Dashboard graphs](images/dashboard-graphs.png)
![Dashboard live readings](images/dashboard-live-readings.png)
⚠️ Limitations
MQ sensors are not selective and need warm-up time; readings are approximate, not certified ppm values.
DHT11 accuracy is limited (about ±2 °C and ±5 % RH).
Video quality and range depend on Wi-Fi.
This is a student prototype and not a certified safety instrument.
🔮 Future Scope
Longer-range communication
More accurate, certified gas sensors
Remote driving of the rover
AI-based hazard detection
📄 Documentation
The full project report is in [`docs/`.](https://docs.google.com/document/d/1o_uY8ymkciPO_Rj7JVLgmSQn-_UYEhEe/edit?usp=drive_link&ouid=110081230152146497675&rtpof=true&sd=true)
👥 Team
Department of Electrical and Electronics Engineering (EEE), S R University, Second Year
Name	Roll No.
Snigdha Kanakam	2505A21024
Pranith Billa	2505A21025
Imran Mohammad	2505A21026
Vivek Korivi	2505A21027
Tharun Bioroju	2505A21028
Hemanth Marri	2505A21029
Rohan Gorantala	2505A21030
Guide / Faculty: add name here
📜 License
Released under the MIT License.
