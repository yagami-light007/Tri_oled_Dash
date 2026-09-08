import time
import socket
import json
import requests
import psutil

ESP32_IP = "192.168.29.113"  
UDP_PORT = 4210
LHM_URL = "http://127.0.0.1:8085/data.json"

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

def parse_temp(val_str):
    try:
        return int(float(val_str.replace("°C", "").strip()))
    except Exception:
        return 0

def find_temps(node):
    cpu = 0
    gpu = 0
    
    text = node.get("Text", "")
    val = node.get("Value", "")

    if text == "Core (Tctl/Tdie)":
        cpu = parse_temp(val)
    elif text == "GPU Core" and "°C" in str(val):
        gpu = parse_temp(val)

    for child in node.get("Children", []):
        c_cpu, c_gpu = find_temps(child)
        if c_cpu > 0: cpu = c_cpu
        if c_gpu > 0: gpu = c_gpu

    return cpu, gpu

print(f"Streaming stats to ESP32 ({ESP32_IP})...")

while True:
    try:
        
        cpu_usage = int(psutil.cpu_percent(interval=0.2))
        ram_usage = int(psutil.virtual_memory().percent)

        
        res = requests.get(LHM_URL, timeout=2)
        cpu_temp, gpu_temp = 0, 0
        
        if res.status_code == 200:
            cpu_temp, gpu_temp = find_temps(res.json())

        
        gpu_usage = 0

        print(f"CPU: {cpu_usage}% ({cpu_temp}°C) | RAM: {ram_usage}% | GPU: ({gpu_temp}°C)")

        payload = {
            "cpu": cpu_usage,
            "cpu_t": cpu_temp,
            "ram": ram_usage,
            "gpu": gpu_usage,
            "gpu_t": gpu_temp
        }

        
        sock.sendto(json.dumps(payload).encode('utf-8'), (ESP32_IP, UDP_PORT))

    except Exception as e:
        print(f"Error: {e}")

    time.sleep(1)