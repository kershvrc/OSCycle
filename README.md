# 🚴 OSCycle v4.5.4

> Turn your stationary bike into a VR controller.

OSCycle is an open-source hardware and software bridge for VR fitness. It reads the reed switch sensor on a stationary bike and converts wheel rotations into OSC (Open Sound Control) data, moving your VRChat avatar in sync with your pedaling. Built for the ESP8266 — a cheap, widely available microcontroller that handles sensor reading, Wi-Fi, and a web dashboard all in one.

---

## 📺 Demo

[![OSCycle Demo Video](https://img.youtube.com/vi/UXpXUJbaroM/maxresdefault.jpg)](https://www.youtube.com/watch?v=UXpXUJbaroM)

---

## ✨ Features

### ⚙️ How Speed Tracking Works
OSCycle measures the time between pulses from the bike's wheel sensor to calculate speed. It uses two modes depending on how fast you're going:

- **Low speed** — calculates velocity pulse-by-pulse. Works well and feels smooth at walking and moderate cycling speeds.
- **High speed** — switches to frequency-based tracking to reduce CPU load and avoid jitter.

**Known limitation:** At high speeds, pulse intervals get very short and readings become less reliable. Speed can plateau or behave inconsistently depending on your debounce setting, sensor quality, and how clean the signal is from your bike. This is an active area we're looking to improve — see the [Roadmap](#-roadmap) section.

### 💬 VRChat Chatbox Integration
Sends real-time workout stats directly to your VRChat chatbox:
- Live speed (toggleable between MPH and KMH)
- Trip distance
- Estimated wattage
- Active workout timer (only counts while pedaling)
- Custom signature text

### 🌐 Web Dashboard
Access the control panel at `http://oscycle.local` — no coding required:
- Live speed and distance graphs
- On-the-fly calibration (wheel circumference, speed multiplier, debounce)
- OSC sensitivity slider with real-time feedback
- Steering, 180° U-Turn macro, reverse toggle, and left/right strafe controls
- Stationary Mode — logs exercise stats without moving your avatar
- 4 switchable UI color themes

### 📡 OTA Updates & Networking
- Flash new firmware wirelessly — no cables required after initial setup
- Supports both DHCP and static IP
- Download `net.bin` and `app.bin` config backups from the dashboard

### ⚡ Power
Currently powered via USB battery bank for portability. The unit can also be wired directly to the bike's internal power supply.
- Most basic bike computers run on 2x AA batteries (3V)
- The ESP8266 runs on 3.3V — if wiring to bike power, use a voltage regulator and verify output stability before connecting

---

## 🔮 Roadmap

### High-Speed Accuracy
The biggest open problem right now. At high cadence, pulse intervals get short enough that debounce settings, signal noise, and ISR timing on the ESP8266 all start to interfere. Speed plateaus and stops reflecting real-world effort accurately.

Some directions worth exploring:
- **Adaptive debounce** — dynamically lowering the debounce threshold as speed increases, rather than using a fixed value
- **Rolling window averaging** — smoothing speed over the last N pulses instead of relying on a single interval

If you have experience with sensor signal processing or ESP8266 ISR timing and want to take a crack at any of these, a Pull Request would be very welcome.

### Heart Rate Monitoring
Most stationary bikes have built-in pulse handles connected via a 3-pole (TRS) 3.5mm jack. Investigation into tapping this data is planned but hasn't started yet.

---

## 🛠️ Hardware Setup

### 📸 Reference Photos

<table>
  <tr>
    <td align="center">
      <img src="https://github.com/user-attachments/assets/d5002f09-f7e5-4c5b-aebb-930fef9e091e" width="250px" alt="PCB Front"/><br/>
      <b>Soldered In</b>
    </td>
    <td align="center">
      <img src="https://github.com/user-attachments/assets/5f4cb5da-51b5-4c5d-b14d-e33df648ca44" width="250px" alt="Wiring Close-up"/><br/>
      <b>Solder Points</b>
    </td>
    <td align="center">
      <img src="https://github.com/user-attachments/assets/72501e6e-b897-40a3-a29d-4b30f439af05" width="250px" alt="Case Assembly"/><br/>
      <b>Testing with 3.5mm to RCA adapter</b>
    </td>
  </tr>
</table>

### Method 1 — In-Line Bridge (Plug-and-Play)
Ideal if your bike uses 3.5mm connectors. Creates a Y-tap so your original bike computer still functions.

| Connection | Wire To |
|---|---|
| Sleeve of Female Jack + Sleeve of Male Plug | GND on ESP8266 |
| Tip of Female Jack + Tip of Male Plug | GPIO 12 (D6) on ESP8266 |

### Method 2 — Direct Solder
A permanent, low-profile solution.

1. Locate the sensor solder pads on the back of your bike's original controller PCB.
2. Solder one wire to the **Ground** pad and one to the **Signal** pad.
3. Connect to **GND** and **GPIO 12 (D6)** on your ESP8266.

---

## 🚀 Installation

### 1. Software Requirements
- Download the [Arduino IDE](https://www.arduino.cc/en/software)
- Install the ESP8266 Board Core:
  - Go to **File → Preferences**
  - Add this URL to *Additional Boards Manager URLs*:
    ```
    http://arduino.esp8266.com/stable/package_esp8266com_index.json
    ```
  - Go to **Tools → Board → Boards Manager**, search `ESP8266`, and install

### 2. Configure
Open the `.ino` sketch and find the `NET_DEFAULTS` section. Replace with your details:
```cpp
"YOUR_WIFI_NAME"
"YOUR_WIFI_PASS"
"YOUR_PC_IP_ADDRESS"
```

### 3. Flash
1. Connect your ESP8266 via USB
2. Select your board under **Tools → Board**
3. Click **Upload**

### 4. Ride
1. Mount the ESP8266 to your bike
2. Open VRChat
3. Enable OSC: **Action Menu → Expressions → Options → OSC → Enabled**
4. Start pedaling 🚴

---

## 🤝 Contributing

I am not a coder — 95% of the logic and optimization in this project was built through collaborative prompting with AI.

Have suggestions for the code, ideas on the high-speed accuracy problem, better ways to handle the 3-pole pulse sensors, or improvements to the dashboard? Submit a Pull Request or open an Issue. Community help is genuinely useful here.
