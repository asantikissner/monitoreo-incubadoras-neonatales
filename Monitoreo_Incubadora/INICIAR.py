import subprocess
import webbrowser
import time
from flask_cors import CORS
import os
import sys
from flask import Flask, jsonify

app = Flask(__name__)
CORS(app)

# Rutas a tus scripts Flask
ruta_servidor_datos = "/Users/antonellasantikissner/Documents/PROYECTO/neosensa_2/Monitoreo_Incubadora/data/servidor_flask_csv.py"
ruta_simulador = "/Users/antonellasantikissner/Documents/PROYECTO/neosensa_2/Monitoreo_Incubadora/scripts/mqtt_simulacion2_csv.py"
PROJECT_DIR = os.path.dirname(os.path.abspath(__file__))
ruta_html = os.path.join(PROJECT_DIR, "data", "login.html")

# Iniciar servidor de lectura de Excel (puerto 5000)
try:
    proc_datos = subprocess.Popen([sys.executable, ruta_servidor_datos], cwd=PROJECT_DIR)
    print("✅ servidor_flask.py iniciado en http://localhost:5000/datos")
    #webbrowser.open("http://localhost:5000/datos")
    time.sleep(2)

except Exception as e:
    print(f"🛑 Error al iniciar servidor_flask.py: {e}")

# Iniciar servidor que recibe datos del ESP32 y simula los sensores faltantes (puerto 5001)
try:
    proc_simulador = subprocess.Popen([sys.executable, ruta_simulador], cwd=PROJECT_DIR)
    print("✅ mqtt_simulacion.py iniciado en http://localhost:5001/datos")
    time.sleep(2)
except Exception as e:
    print(f"🛑 Error al iniciar mqtt_simulacion.py: {e}")

# Abrir login.html en navegador
webbrowser.open(f"file://{ruta_html}")

# Mantener los procesos corriendo
try:
    proc_datos.wait()
    proc_simulador.wait()
    
except KeyboardInterrupt:
    print("\n🛑 Terminando procesos...")
    proc_datos.terminate()
    proc_simulador.terminate()
