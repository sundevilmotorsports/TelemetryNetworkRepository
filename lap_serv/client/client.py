import requests
import json
import re

ESP32_IP = "192.168.4.1"
SSE_URL = f"http://{ESP32_IP}/events"

# Connects to the ESP32 and "processes" the SSE stream
def get_sse_data():
    print(f"Connecting to SSE stream at {SSE_URL}...")
    try:
        # stream=True keeps connection open
        with requests.get(SSE_URL, stream=True) as response:
            # Check for successful
            if response.status_code != 200:
                print(f"Error: Received status code {response.status_code}")
                return

            print("Connected. Waiting for events...")
            # Iterate over the response lines to get the event data
            for line in response.iter_lines(decode_unicode=True):
                # An empty line signifies the end of a message
                if line and line.startswith("data: "):
                    data = line[len("data: "):]
                    print(f"Received: {data}")

    except requests.exceptions.RequestException as e:
        print(f"An error occurred: {e}")

if __name__ == "__main__":
    get_sse_data()
