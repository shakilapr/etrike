@echo off
cd /d C:\projects\etrike\control-toolkit\backend
echo === start %date% %time% ===>> _uvicorn8001.out.log
python -m uvicorn control_toolkit.main:app --host 127.0.0.1 --port 8001 --log-level info 1>>_uvicorn8001.out.log 2>>_uvicorn8001.err.log
