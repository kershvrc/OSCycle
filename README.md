🚴 OSCycle

Turn your stationary bike into a high-performance VR controller.

OSCycle is an open-source hardware and software bridge designed for VR fitness. It converts real-world physical activity on a stationary bike into OSC (Open Sound Control) data, allowing your VRChat avatar to move in sync with your pedaling. Built for the ESP8266, it’s a low-cost, highly customizable alternative to commercial VR fitness trackers.

## 📺 OSCycle in Action
[<img src="https://img.youtube.com/vi/UXpXUJbaroM/maxresdefault.jpg" width="100%" alt="OSCycle Demo Video">](https://www.youtube.com/watch?v=UXpXUJbaroM)
*Click the image above to watch the demo on YouTube!*

✨ Core Features
⚙️ Hybrid Velocity Engine

The "brain" of the unit intelligently tracks movement using two distinct modes:

    Low-Speed Fluidity: At walking speeds, it calculates movement pulse-by-pulse for a smooth, stutter-free experience.

    High-Speed Stability: At sprinting speeds, it switches to frequency tracking to ensure rock-solid data and zero CPU lag.

💬 VRChat Chatbox Integration

Broadcast your workout progress to the world! OSCycle sends real-time stats directly to your VRChat chatbox:

    Live Speed: Toggleable between MPH and KMH.

    Distance: Total trip distance tracked in real-time.

    Estimated Wattage: Power output calculation based on speed.

    Active Workout Timer: A smart timer that only counts while you are actually pedaling.

    Custom Signature: Add a personal brand or shout-out to every message.

🌐 Web-Based Dashboard

No coding required for daily use. Access the control panel via http://oscycle.local to:

    View Live Graphs of speed and distance.

    Perform On-the-fly Calibration (Wheel circumference, multipliers, debounce).

    Use Remote Controls for steering, a 180° U-Turn macro, and a Reverse toggle.

    Enable Stationary Mode to record exercise stats without moving your avatar in-game.

📡 Over-The-Air (OTA) & Networking

    Wireless Updates: Flash new firmware versions over Wi-Fi—no cables required.

    Flexible Networking: Supports both DHCP and Static IP configurations.

    Backups: Download your configuration files (net.bin and app.bin) to save your settings.
	
⚡ Power Management

Currently, the unit is powered via a standard USB Battery Bank for portability and ease of testing. However, the system can be wired to draw power directly from the bike's internal circuitry.

    Bike Power: Most basic bike computers (including the one used for this project) run on 2x AA batteries (3V).

    Note: While the ESP8266 can run on 3.3V, I have not yet wired the power into the original controller. If you attempt this, ensure you are using a voltage regulator or verifying that the bike's power output is stable enough for the ESP8266’s Wi-Fi spikes.

🔮 Future Roadmap (Experimental)

    Pulse/Heart Rate Monitoring: Most stationary bikes include built-in pulse handles that connect via a 3-pole (TRS) 3.5mm jack.

    Investigation Needed: I haven't investigated these pins yet, but the goal is to tap into the heart rate data

🛠️ Hardware Setup


### 📸 Hardware & PCB Reference
<table>
  <tr>
    <td align="center">
      <img src="https://github.com/user-attachments/assets/d5002f09-f7e5-4c5b-aebb-930fef9e091e" width="250px" alt="PCB Front"/><br />
      <b>Soldered in</b>
    </td>
    <td align="center">
      <img src="https://github.com/user-attachments/assets/5f4cb5da-51b5-4c5d-b14d-e33df648ca44" width="250px" alt="Wiring Close-up"/><br />
      <b>Solder Points</b>
    </td>
    <td align="center">
      <img src="https://github.com/user-attachments/assets/72501e6e-b897-40a3-a29d-4b30f439af05" width="250px" alt="Case Assembly"/><br />
      <b>Testing before I solder using 3.5mm to rca adapter</b>
    </td>
  </tr>
</table>
Method 1: The "In-Line Bridge" (Plug-and-Play)

Ideal if your bike uses 3.5mm connectors. This creates a "Y-tap" so your original bike computer still works.

    The Shared Ground: Connect the Sleeve of the Female Jack, the Sleeve of the Male Plug, and GND on the ESP8266.

    The Signal Tap: Connect the Tip of the Female Jack, the Tip of the Male Plug, and GPIO 12 (D6) on the ESP8266.


Method 2: The "Direct Solder" (The Kersh Method)

A permanent, low-profile solution.

    Locate the sensor solder pads on the back of your bike’s original controller PCB.

    Solder one wire to the Ground pad and the other to the Signal pad.

    Connect these to GND and GPIO 12 (D6) on your ESP8266.

🚀 Installation & Flashing
1. Software Requirements

    Download the Arduino IDE.

    Install the ESP8266 Board Core:

        Go to File > Preferences.

        Add http://arduino.esp8266.com/stable/package_esp8266com_index.json to the Additional Boards Manager URLs.

        Go to Tools > Board > Boards Manager, search for ESP8266, and install.

2. Configure

Open the .ino sketch and navigate to the NET_DEFAULTS section. Replace the placeholders with your specific details:

    YOUR_WIFI_NAME

    YOUR_WIFI_PASS

    YOUR_PC_IP_ADDRESS

3. Flash

    Connect your ESP8266 to your PC via USB.

    Select your board under Tools > Board.

    Click Upload.

4. Ride

    Mount the ESP8266 to your bike.

    Open VRChat.

    Enable OSC in the Action Menu (Expressions > Options > OSC > Enabled).

    Start pedaling! 🚴


🤝 Contribution & Support

I am not a coder. 95% of the logic and optimization in this project was generated through collaborative prompting with Google Gemini.

If you have suggestions for the code, better ways to handle the 3-pole pulse sensors, or improvements to the web dashboard, please submit a Pull Request or open an Issue! Community help is more than welcome to make this the best VR fitness tool possible.
