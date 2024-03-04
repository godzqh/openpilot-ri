from cereal import messaging, log
from openpilot.common.params import Params
from openpilot.selfdrive.frogpilot.fleetmanager.helpers import get_last_lon_lat
from openpilot.system.assistant.rev_speechd import AssistantWidgetControl
import json

STTState = log.SpeechToText.State

sm = messaging.SubMaster(["speechToText"])
import time
import os
import re
import requests
import urllib.parse

def get_coordinates_from_transcript(transcript, proximity, mapbox_access_token):
  # Regular expression to find 'navigate to' or 'directions to' followed by an address
  pattern = r'\b(navigate to|directions to)\b\s+(.*?)(\.|$)'
  # Search for the pattern in the transcript
  match = re.search(pattern, transcript, re.IGNORECASE)
  if match:
    address = match.group(2).strip()
    dests = Params().get("ApiCache_NavDestinations", encoding='utf-8')
    dests = json.loads(dests)
    for place in dests:
      if "label" in place and place["label"] in [address]:
        # Return the coordinates of home or work
        return {
          "longitude": place['longitude'],
          "latitude": place['latitude'],
          "place_name": place['place_name']
        }
    encoded_address = urllib.parse.quote(address)
    mapbox_url = f"https://api.mapbox.com/geocoding/v5/mapbox.places/{encoded_address}.json?proximity={proximity[0]},{proximity[1]}&access_token={mapbox_access_token}&limit=1"
    print(mapbox_url)
    print(address)
    response = requests.get(mapbox_url)
    if response.status_code == 200:
      data = response.json()
      # Assuming the first result is the most relevant
      if data['features']:
        coordinates = {
          "longitude": data['features'][0]['geometry']['coordinates'][0],
          "latitude": data['features'][0]['geometry']['coordinates'][1],
          "place_name": address
        }
        print(coordinates)
        return coordinates
      print("No coordinates")
    print(f"Mapbox API error: {response.status_code}")
  return False

def main():
  mapbox_access_token = Params().get("MapboxPublicKey",encoding='utf-8')
  while True:
    dest = False
    transcript: str = ""
    sm.update(0)
    if sm.updated["speechToText"]:
      transcript = sm["speechToText"].transcript
      if not sm["speechToText"].state == log.SpeechToText.State.final:
        print(f'Interim result: {transcript}')
      else:
        print(f'Final result: {transcript}')
        print(mapbox_access_token)
        dest = get_coordinates_from_transcript(transcript, get_last_lon_lat(), mapbox_access_token)
        if dest:
          Params().put("NavDestination", json.dumps(dest))
          print(dest)
          dest = False
    time.sleep(1)

if __name__ == "__main__":
    main()

