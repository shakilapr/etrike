# Running the Control-UI

The Control-UI consists of a Python FastAPI backend and a Vite+React frontend. Both must be running simultaneously to use the application.

## 1. Start the Backend (FastAPI + WebSockets)
The backend requires Python 3.11+.

1. Open a terminal and navigate to the backend directory:
   ```bash
   cd control-ui/backend
   ```
2. (Optional but recommended) Activate your virtual environment:
   ```bash
   # Windows PowerShell
   .venv\Scripts\Activate.ps1
   ```
3. Run the development server:
   ```bash
   uvicorn main:app --reload --port 8000
   ```
   *The backend will now be listening for WebSocket connections at `ws://localhost:8000/api/stream`.*

## 2. Start the Frontend (Vite + React)
The frontend requires Node.js (v20+ recommended).

1. Open a **second** terminal and navigate to the frontend directory:
   ```bash
   cd control-ui/frontend
   ```
2. Run the development server:
   ```bash
   npm run dev
   ```
3. Open your browser and navigate to:
   **http://localhost:5173**

## 3. Hardware Requirements (Phase 4)
For physical hardware bench testing:
- Ensure the **CANalyst-II USB adapter** is plugged into your PC.
- Ensure the vehicle/bench ECUs are powered on and connected to the high/low CAN buses.
- Ground the hardware bypass pin on the ECUs to prevent timeout ESTOPs if running without full system sensors.
- The Python backend will automatically detect the CANalyst-II adapter on startup. If no physical adapter is found, it will throw an error (or use the mock/virtual adapter if configured to do so for testing).
