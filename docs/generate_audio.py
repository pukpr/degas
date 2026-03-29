import os
import re
from gtts import gTTS
from pydub import AudioSegment

def parse_srt(srt_file):
    with open(srt_file, 'r', encoding='utf-8') as f:
        content = f.read()
    
    # Simple regex to parse SRT: Index, Time range, Text
    pattern = re.compile(r'(\d+)\n(\d{2}:\d{2}:\d{2},\d{3}) --> (\d{2}:\d{2}:\d{2},\d{3})\n((?:.|\n)*?)(?=\n\n|\Z)')
    matches = pattern.findall(content)
    
    subtitles = []
    for match in matches:
        index = int(match[0])
        text = match[3].strip().replace('\n', ' ')
        subtitles.append({
            'index': index,
            'text': text
        })
    return subtitles

def ms_to_srt_time(ms):
    h = int(ms // 3600000)
    ms %= 3600000
    m = int(ms // 60000)
    ms %= 60000
    s = int(ms // 1000)
    ms %= 1000
    return f"{h:02d}:{m:02d}:{s:02d},{int(ms):03d}"

def generate_audio_track_and_updated_srt(subtitles, audio_output, srt_output, total_duration_s):
    total_duration_ms = total_duration_s * 1000
    num_subs = len(subtitles)
    
    # We want to spread the subs across the total duration.
    # Let's say each sub gets a starting point at regular intervals.
    interval_ms = total_duration_ms / num_subs
    
    updated_subs = []
    full_audio = AudioSegment.silent(duration=total_duration_ms)
    
    for i, sub in enumerate(subtitles):
        start_time = i * interval_ms
        
        print(f"Generating audio for subtitle {sub['index']}: {sub['text'][:30]}...")
        tts = gTTS(text=sub['text'], lang='en')
        temp_file = f"temp_{sub['index']}.mp3"
        tts.save(temp_file)
        
        segment = AudioSegment.from_mp3(temp_file)
        tts_duration = len(segment)
        
        # Overlay the segment
        full_audio = full_audio.overlay(segment, position=start_time)
        
        updated_subs.append({
            'index': sub['index'],
            'start': start_time,
            'end': start_time + tts_duration,
            'text': sub['text']
        })
        os.remove(temp_file)
        
    full_audio.export(audio_output, format="mp3")
    
    with open(srt_output, 'w', encoding='utf-8') as f:
        for sub in updated_subs:
            f.write(f"{sub['index']}\n")
            f.write(f"{ms_to_srt_time(sub['start'])} --> {ms_to_srt_time(sub['end'])}\n")
            f.write(f"{sub['text']}\n\n")

if __name__ == "__main__":
    subs = parse_srt("comparison_commentary.srt")
    # Total duration 135.85s from ffprobe
    generate_audio_track_and_updated_srt(subs, "commentary_audio.mp3", "comparison_commentary_updated.srt", 135.85)
