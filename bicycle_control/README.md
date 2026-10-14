# Bicycle Control - wiring aur process

ESP32-C6 (Pico-H) + TB6612FNG + inductor/capacitor se e-bike controller ko
0..3.4V throttle signal dena. WiFi: `bicycle control`, password `bicycle123`,
page `http://192.168.4.1`.

## Parts
- ESP32-C6 Pico-H
- TB6612FNG module
- Inductor 100-220uH, >= 1A rating
- Capacitor 220uF (>= 10V) + 100nF ceramic
- 10k resistor (STBY pull-down)
- 5V supply (>= 1A), common GND

## Pinout (TB6612 module, naam apne board se match karo)
| Pin | Kaam | Connect |
|---|---|---|
| VM | output supply | 5V supply (+) |
| VCC | logic supply | ESP32 3V3 |
| GND (dono) | ground | ESP32 GND + 5V supply (-) + controller GND |
| PWMA | PWM input | ESP32 GPIO0 |
| AIN1 | HIGH rakho | ESP32 GPIO1 |
| AIN2 | LOW rakho | ESP32 GPIO2 |
| STBY | driver on/off | ESP32 GPIO3 + 10k se GND |
| AO1 | PWM output | inductor ka ek sira |
| AO2 | - | khali |

## Output filter
```
AO1 --[Inductor]--+--> controller throttle signal wire
                  |
            [220uF + 100nF]
                  |
                 GND
```

## Process
1. Pehle controller se mat jodo. Output ko multimeter / 4.7 ohm 2W resistor par test karo.
2. Code upload karo, phone se WiFi connect karo, START dabao, slider chalao.
3. Output voltage dekho (3V par ~3V). Load se 0.1-0.2V kam ho sakta hai (open-loop).
4. Controller ki original throttle ki signal wire alag karo, output ko signal wire se jodo.
   Throttle ka 5V aur GND controller ke hi rakho; GND common hona zaroori hai.
5. Bike stand par, pahiya hawa me rakh kar test karo.
