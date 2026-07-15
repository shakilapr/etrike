import os
import json

log_path = r'C:\Users\ASUS\.gemini\antigravity\brain\ed4f10d7-fb20-4b63-8b40-de6b0361174b\.system_generated\logs\transcript_full.jsonl'
output_path = r'c:\projects\etrike\qa_recover.txt'

with open(log_path, 'r', encoding='utf-8') as f:
    for line in f:
        try:
            data = json.loads(line)
            if data.get('type') == 'TOOL_RESPONSE':
                content = data.get('content', '')
                if 'Total Lines: 2781' in content or 'architecture-control-toolkit.md' in content or 'architecture-vt-console.md' in content:
                    if '# E-Trike Control Toolkit Architecture' in content or '# VT-Console Architecture' in content:
                        with open(output_path, 'w', encoding='utf-8') as out_f:
                            out_f.write(content)
                        print("Recovered!")
                        break
        except Exception as e:
            pass
