import requests
import json
import re
import time
from sseclient import SSEClient

ESP32_IP = "192.168.4.1"
SSE_URL = f"http://{ESP32_IP}/events"

if __name__ == "__main__":
    print(f"Connecting to SSE stream at {SSE_URL}...")
    try:
        # Pass the URL directly to SSEClient; it handles the requests call
        client = SSEClient(SSE_URL)
        count = 0
        for event in client:
            count += 1
            print(f"{count}. Received: {event.data}")
    except requests.exceptions.RequestException as e:
        print(f"Failed to connect or connection lost: {e}")
