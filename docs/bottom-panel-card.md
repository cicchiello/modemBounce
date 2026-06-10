# Bottom-panel reference card

Print at **85 × 54 mm** (business card), laminate, and affix to the bottom of the enclosure.

---

<!-- ============================================================
     CARD CONTENT BELOW — print/render from this point
     Suggested: render to PDF at 85×54 mm, ~9 pt font
     ============================================================ -->

---

**BOUNCY** · Network Monitor v1.2

---

**LED STATUS**

|-----------------|--------------------------------|
| 🟢 Green        | Monitoring normally            |
| 🔵 Blue         | Connecting to Wi-Fi            |
| 🔴 Red flashing | Provisioning — see setup below |
| 🔴 Red steady   | Bouncing a device              |
---------------------------------------------------|

**NORMAL OPERATION**  
Checks every 30 min: Wi-Fi → router → modem → 8.8.8.8 → DNS  
Retries 5× before bouncing. Alert email sent after each bounce.

---

**FIRST-TIME SETUP / AFTER RESET**  
1. Join Wi-Fi network: **`Bouncy-Setup`**  
2. Open browser → **`http://192.168.1.1`**  
3. Enter SSID, password, run mode → Save

---

**FACTORY RESET**  
Hold reset button **5 seconds** (until LED flashes red)  
Erases stored Wi-Fi credentials → re-enters setup

---

*modemBounce · github.com/jcicchiello · 2026*

---
