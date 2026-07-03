# mqtt_simulacion2.py (CSV + multi-ESP)
import threading, time, json, random, os, csv, sys
from datetime import datetime
from flask import Flask, request, jsonify
from flask_cors import CORS
from pathlib import Path
import paho.mqtt.client as mqtt

PROJECT_DIR = Path(__file__).resolve().parents[1]
if str(PROJECT_DIR) not in sys.path:
    sys.path.insert(0, str(PROJECT_DIR))

from data.auditoria import LIMIT_KEYS, registrar_cambio_limites, sesion_activa

app = Flask(__name__)
CORS(app)

DATA_DIR = os.path.join(os.path.dirname(__file__), "data")
os.makedirs(DATA_DIR, exist_ok=True)

# límites file
limites_path = PROJECT_DIR / "limites.json"
DEFAULTS = {
    "Tmin": 36, "Tmax": 37, "Ttol": 1,
    "Hmin": 30, "Hmax": 50, "Htol": 5,
    "Imin": 0.35, "Imax": 1.5, "Itol": 0.1,
    "Omin": 22, "Omax": 80, "Otol": 10,
}


# valores de simulación base (se usan para cada esp; podés variar)
base = {
    "temperatura": 37.0,
    "humedad": 90.0,
    "iluminancia": 1.0,
    "oxigeno": 90.0
}

PROBABILIDAD_ALARMA_SIMULADA = 0.05
PARAMETROS_SIMULADOS = ("T", "H", "I", "O")

def csv_prepend(path, rowdict, fieldnames):
    """Inserta la fila al inicio (simula insert_rows en Excel) sin duplicar headers."""
    if not os.path.exists(path):
        # Crear archivo nuevo con header
        with open(path, 'w', newline='', encoding='utf-8') as f:
            writer = csv.DictWriter(f, fieldnames=fieldnames)
            writer.writeheader()
            writer.writerow(rowdict)
        return

    # Leer contenido existente
    with open(path, 'r', newline='', encoding='utf-8') as f:
        lines = f.readlines()

    # Si la primera línea ya es header, no lo agregamos de nuevo
    if lines and lines[0].strip() == ",".join(fieldnames):
        new_content = [lines[0]] + [",".join([str(rowdict.get(fn, "")) for fn in fieldnames]) + "\n"] + lines[1:]
    else:
        new_content = [",".join(fieldnames) + "\n"] + [",".join([str(rowdict.get(fn, "")) for fn in fieldnames]) + "\n"] + lines

    # Reescribir archivo
    with open(path, 'w', newline='', encoding='utf-8') as f:
        f.writelines(new_content)

# MQTT callbacks
def on_connect(client, userdata, flags, rc):
    print("🔌 Conectado al broker MQTT (simulador)")
    client.subscribe("config/limites")
    client.subscribe("sensor/+/datos")

def valores_limites_desde_payload(data, prev):
    return {k: data.get(k, prev.get(k)) for k in LIMIT_KEYS}

def guardar_nuevos_limites(data):
    esp = str(data.get("esp", "1"))
    all_limits = limites_path.exists() and json.loads(limites_path.read_text()) or {}
    prev = all_limits.get(esp, DEFAULTS)
    nuevos = valores_limites_desde_payload(data, prev)
    all_limits[esp] = nuevos
    limites_path.write_text(json.dumps(all_limits, indent=2))
    print(f"📥 Nuevos límites para ESP {esp}: {nuevos}")
    return prev, nuevos

def on_message(client, userdata, msg):
    try:
        if msg.topic.startswith("sensor/") and msg.topic.endswith("/datos"):
            data = json.loads(msg.payload.decode())
            esp = str(data.get("esp", "1"))
            print(f"MQTT recibido de esp {esp}")
            # guardar en CSV por esp
            fieldnames = ["fecha","temperatura","ley_T","humedad","ley_H","iluminancia","ley_I","oxigeno","ley_O"]
            row = {
                "fecha": data.get("fecha"),
                "temperatura": data.get("temperatura"),
                "ley_T": data.get("ley_T"),
                "humedad": data.get("humedad"),
                "ley_H": data.get("ley_H"),
                "iluminancia": data.get("iluminancia"),
                "ley_I": data.get("ley_I"),
                "oxigeno": data.get("oxigeno"),
                "ley_O": data.get("ley_O")
            }
            path = os.path.join(DATA_DIR, f"incubadora_{esp}.csv")
            csv_prepend(path, row, fieldnames)

        elif msg.topic == "config/limites":
            data = json.loads(msg.payload.decode())
            guardar_nuevos_limites(data)   # 👈 actualiza limites.json por esp_id
            print("✔️ Limites actualizados vía MQTT:", data)

    except Exception as e:
        print("❌ Error procesando MQTT:", e)


cliente_mqtt = mqtt.Client()
cliente_mqtt.on_connect = on_connect
cliente_mqtt.on_message = on_message
cliente_mqtt.connect("broker.emqx.io", 1883)
cliente_mqtt.loop_start()

@app.route("/limites", methods=["GET", "POST"])
def enviar_limites_actuales():
    if request.method == "GET":
        esp = request.args.get("esp", "1")
        if limites_path.exists():
            all_limits = json.loads(limites_path.read_text())
            return jsonify(all_limits.get(esp, DEFAULTS)), 200
        return jsonify(DEFAULTS), 200

    data = request.get_json(silent=True) or {}
    session_id = request.headers.get("X-Session-Id") or data.get("session_id")
    sesion = sesion_activa(session_id)
    if not sesion:
        return jsonify({"ok": False, "error": "Sesión inválida"}), 401

    esp = str(data.get("esp", "1"))
    faltantes = [key for key in LIMIT_KEYS if key not in data]
    if faltantes:
        return jsonify({"ok": False, "error": f"Faltan límites: {', '.join(faltantes)}"}), 400

    prev, nuevos = guardar_nuevos_limites(data)
    auditoria = registrar_cambio_limites(session_id, esp, prev, nuevos)
    return jsonify({
        "ok": True,
        "esp": esp,
        "limites": nuevos,
        "cambio_limites": bool(auditoria.get("cambios")),
        "cambios": auditoria.get("cambios", {}),
    }), 200

# simulador multi-ESP
def simular_ruido(valor_anterior, max_variacion):
    return round(valor_anterior + random.uniform(-max_variacion, max_variacion), 2)

def limitar(valor, minimo, maximo):
    return min(max(valor, minimo), maximo)

def valor_normal(minimo, maximo, decimales=2):
    span = maximo - minimo
    if span <= 0:
        return round(minimo, decimales)

    margen = span * 0.2
    bajo = minimo + margen
    alto = maximo - margen
    if bajo >= alto:
        bajo, alto = minimo, maximo

    return round(random.uniform(bajo, alto), decimales)

def valor_en_alarma(minimo, maximo, tolerancia, decimales=2):
    span = maximo - minimo
    direccion = random.choice(("baja", "alta"))
    separacion = max(tolerancia * random.uniform(1.2, 2.0), span * 0.08)

    if direccion == "baja":
        return round(minimo - separacion, decimales)
    return round(maximo + separacion, decimales)

def simulador_esp(esp_id, interval=60):
    """Publica datos simulados por MQTT para esp_id cada `interval` segundos."""
    # variables locales por esp (pueden partir de base y variar)
    temperatura = base["temperatura"] + random.uniform(-0.5, 0.5)
    humedad = base["humedad"] + random.uniform(-2, 2)
    iluminancia = base["iluminancia"] + random.uniform(-0.05, 0.05)
    oxigeno = base["oxigeno"] + random.uniform(-1, 1)
    fieldnames = ["fecha","temperatura","ley_T","humedad","ley_H","iluminancia","ley_I","oxigeno","ley_O"]

    while True:
        esp = str(esp_id)
        # leer límites
        if limites_path.exists():
            with open(limites_path, "r") as f:
                all_limits = json.load(f)
                limites = all_limits.get(esp, DEFAULTS)
        else:
            limites = DEFAULTS

        Tmin, Tmax, Ttol = limites["Tmin"], limites["Tmax"], limites["Ttol"]
        Hmin, Hmax, Htol = limites["Hmin"], limites["Hmax"], limites["Htol"]
        Imin, Imax, Itol = limites["Imin"], limites["Imax"], limites["Itol"]
        Omin, Omax, Otol = limites["Omin"], limites["Omax"], limites["Otol"]

        # Simular valores normalmente dentro de rango y forzar una alarma ocasional.
        parametro_en_alarma = random.choice(PARAMETROS_SIMULADOS) if random.random() < PROBABILIDAD_ALARMA_SIMULADA else None

        temperatura = valor_normal(Tmin, Tmax)
        humedad = valor_normal(Hmin, Hmax)
        iluminancia = valor_normal(Imin, Imax)
        oxigeno = valor_normal(Omin, Omax)

        if parametro_en_alarma == "T":
            temperatura = valor_en_alarma(Tmin, Tmax, Ttol)
        elif parametro_en_alarma == "H":
            humedad = valor_en_alarma(Hmin, Hmax, Htol)
        elif parametro_en_alarma == "I":
            iluminancia = valor_en_alarma(Imin, Imax, Itol)
        elif parametro_en_alarma == "O":
            oxigeno = valor_en_alarma(Omin, Omax, Otol)

        alarma_T = True if ((temperatura < (Tmin-Ttol)) or (temperatura > (Tmax+Ttol))) else False
        alarma_H = True if (humedad < (Hmin-Htol) or humedad > (Hmax+Htol)) else False
        alarma_I = True if ((iluminancia < (Imin-Itol)) or (iluminancia > (Imax+Itol))) else False
        alarma_O = True if ((oxigeno < (Omin-Otol)) or (oxigeno > (Omax+Otol))) else False

        ley_T = "alta" if (temperatura > Tmax and alarma_T) else "baja" if (temperatura < Tmin and alarma_T) else "-"
        ley_H = "alta" if (humedad > Hmax and alarma_H) else "baja" if (humedad < Hmin and alarma_H) else "-"
        ley_I = "alta" if (iluminancia > Imax and alarma_I) else "baja" if (iluminancia < Imin and alarma_I) else "-"
        ley_O = "alto" if (oxigeno > Omax and alarma_O) else "bajo" if (oxigeno < Omin and alarma_O) else "-"

        fecha = datetime.now().isoformat(timespec='seconds')
        datos = {
            "fecha": fecha,
            "temperatura": round(temperatura, 2),
            "humedad": round(humedad, 2),
            "iluminancia": round(iluminancia, 2),
            "oxigeno": round(oxigeno, 2),
            "Tmax": Tmax, "Tmin":Tmin, "Ttol":Ttol,
            "Hmax":Hmax, "Hmin":Hmin, "Htol":Htol,
            "Imax":Imax, "Imin":Imin, "Itol":Itol,
            "Omax":Omax, "Omin":Omin, "Otol":Otol,
            "alarma_T": alarma_T,
            "alarma_H": alarma_H,
            "alarma_I": alarma_I,
            "alarma_O": alarma_O,
            "ley_T":ley_T,
            "ley_H":ley_H,
            "ley_I":ley_I,
            "ley_O":ley_O,
            "esp": int(esp_id),
        }

        # publicar por MQTT
        cliente_mqtt.publish(f"sensor/{esp}/datos", json.dumps(datos))

        # también escribir en CSV localmente (igual que on_message)
        row = {
            "fecha": datos["fecha"],
            "temperatura": datos["temperatura"],
            "ley_T": datos["ley_T"],
            "humedad": datos["humedad"],
            "ley_H": datos["ley_H"],
            "iluminancia": datos["iluminancia"],
            "ley_I": datos["ley_I"],
            "oxigeno": datos["oxigeno"],
            "ley_O": datos["ley_O"]
        }
        path = os.path.join(DATA_DIR, f"incubadora_{esp}.csv")

        time.sleep(interval)

def start_simulators(max_esp=39, start=2, interval=10): ##max es EL NUMERO DE INCUBADORA MAS ALTO, NO LA CANTIDAD
    """Lanza simuladores en background para los ESPs start..max_esp (incl). Máx 39."""
    max_esp = min(max_esp, 39)
    for esp in range(start, max_esp+1):
        t = threading.Thread(target=simulador_esp, args=(esp, interval), daemon=True)
        t.start()
    print(f"Simuladores arrancados para ESPs {start}..{max_esp}")

if __name__ == "__main__":
    # arrancar simulación (ajustá max_esp a lo que necesites, por ejemplo 39)
    threading.Thread(target=lambda: start_simulators(max_esp=30, start=7, interval=10), daemon=True).start()
    print("✅ MQTT Simulador iniciado. Escuchando y publicando varios ESPs.")
    app.run(host="0.0.0.0", port=5001)
