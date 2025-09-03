import requests
import json
import re
import time
from sseclient import SSEClient

ESP32_IP = "192.168.4.1"
SSE_URL = f"http://{ESP32_IP}/events"

response = requests.get(SSE_URL, stream=True)
client = SSEClient(response)

# Connects to the ESP32 and "processes" the SSE stream
def get_sse_data():
    print(f"Connecting to SSE stream at {SSE_URL}...")
    while True:
        try:
            with requests.get(SSE_URL, stream=True) as response:
                for line in response.iter_lines(decode_unicode=True):
                    if line and line.startswith("data: "):
                        print(line[len("data: "):])
        except requests.exceptions.RequestException:
            print("Connection lost. Reconnecting...")
        time.sleep(1)

if __name__ == "__main__":
    #get_sse_data()
    for event in client.events():
        print(f"Received: {event.data}")
