import json
import os
import uuid
from datetime import datetime
from pathlib import Path

from werkzeug.security import check_password_hash, generate_password_hash


BASE_DIR = Path(__file__).resolve().parent
USERS_PATH = BASE_DIR / "usuarios.json"
SESSION_HISTORY_PATH = BASE_DIR / "historial_sesiones.json"

INSTITUTIONAL_DOMAIN = "@itba.edu.ar"
DEFAULT_ADMIN_EMAIL = os.environ.get("NEOSENSA_ADMIN_EMAIL", "admin@itba.edu.ar").strip().lower()
DEFAULT_ADMIN_PASSWORD = os.environ.get("NEOSENSA_ADMIN_PASSWORD", "admin123")

LIMIT_KEYS = [
    "Tmin", "Tmax", "Ttol",
    "Hmin", "Hmax", "Htol",
    "Imin", "Imax", "Itol",
    "Omin", "Omax", "Otol",
]


def ahora_iso():
    return datetime.now().isoformat(timespec="seconds")


def leer_json(path, default):
    if not path.exists():
        return default
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return default


def escribir_json(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp_path = path.with_suffix(path.suffix + ".tmp")
    tmp_path.write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding="utf-8")
    tmp_path.replace(path)


def asegurar_archivo_usuarios():
    if USERS_PATH.exists():
        return
    usuarios = {
        DEFAULT_ADMIN_EMAIL: {
            "nombre": "Administrador",
            "password_hash": generate_password_hash(DEFAULT_ADMIN_PASSWORD),
        }
    }
    escribir_json(USERS_PATH, {"usuarios": usuarios})


def parece_hash_password(valor):
    if not isinstance(valor, str):
        return False
    return valor.startswith(("scrypt:", "pbkdf2:", "argon2:")) and "$" in valor


def cargar_usuarios():
    asegurar_archivo_usuarios()
    data = leer_json(USERS_PATH, {"usuarios": {}})
    usuarios = data.get("usuarios", data)
    normalizados = {}
    hubo_cambios = False

    for email, info in usuarios.items():
        email_normalizado = email.strip().lower()
        if isinstance(info, str):
            info = {"password": info}

        info = dict(info)
        password_plana = info.pop("password", None)
        if password_plana:
            info["password_hash"] = generate_password_hash(password_plana)
            hubo_cambios = True
        elif info.get("password_hash") and not parece_hash_password(info.get("password_hash")):
            info["password_hash"] = generate_password_hash(info["password_hash"])
            hubo_cambios = True

        normalizados[email_normalizado] = info

    if hubo_cambios or data.get("usuarios") != normalizados:
        escribir_json(USERS_PATH, {"usuarios": normalizados})

    return normalizados


def validar_credenciales(email, password):
    email = (email or "").strip().lower()
    if not email.endswith(INSTITUTIONAL_DOMAIN):
        return None

    usuarios = cargar_usuarios()
    usuario = usuarios.get(email)
    if not usuario:
        return None

    password_hash = usuario.get("password_hash")
    if not password_hash or not check_password_hash(password_hash, password or ""):
        return None

    return {
        "email": email,
        "nombre": usuario.get("nombre", email),
    }


def cargar_historial():
    data = leer_json(SESSION_HISTORY_PATH, {"sesiones": []})
    sesiones = data if isinstance(data, list) else data.get("sesiones", [])
    return {"sesiones": sesiones}


def guardar_historial(data):
    escribir_json(SESSION_HISTORY_PATH, data)


def crear_sesion(email, nombre=None, ip=None, user_agent=None):
    data = cargar_historial()
    sesion = {
        "session_id": uuid.uuid4().hex,
        "email": email,
        "nombre": nombre or email,
        "inicio": ahora_iso(),
        "fin": None,
        "ultima_actividad": ahora_iso(),
        "activa": True,
        "ip": ip,
        "user_agent": user_agent,
        "cambio_limites": False,
        "cambios_limites": [],
    }
    data["sesiones"].append(sesion)
    guardar_historial(data)
    return sesion


def buscar_sesion(session_id):
    if not session_id:
        return None
    data = cargar_historial()
    for sesion in data["sesiones"]:
        if sesion.get("session_id") == session_id:
            return sesion
    return None


def sesion_activa(session_id):
    sesion = buscar_sesion(session_id)
    if not sesion or not sesion.get("activa"):
        return None
    return sesion


def cerrar_sesion(session_id):
    data = cargar_historial()
    for sesion in data["sesiones"]:
        if sesion.get("session_id") == session_id and sesion.get("activa"):
            ahora = ahora_iso()
            sesion["fin"] = ahora
            sesion["ultima_actividad"] = ahora
            sesion["activa"] = False
            guardar_historial(data)
            return sesion
    return None


def actualizar_actividad(session_id):
    data = cargar_historial()
    for sesion in data["sesiones"]:
        if sesion.get("session_id") == session_id and sesion.get("activa"):
            sesion["ultima_actividad"] = ahora_iso()
            guardar_historial(data)
            return sesion
    return None


def registrar_cambio_limites(session_id, esp, anteriores, nuevos):
    data = cargar_historial()
    cambios = {}

    for key in LIMIT_KEYS:
        anterior = anteriores.get(key)
        nuevo = nuevos.get(key)
        if anterior != nuevo:
            cambios[key] = {
                "antes": anterior,
                "despues": nuevo,
            }

    if not cambios:
        return {"registrado": False, "cambios": {}}

    for sesion in data["sesiones"]:
        if sesion.get("session_id") == session_id and sesion.get("activa"):
            registro = {
                "fecha": ahora_iso(),
                "esp": str(esp),
                "cambios": cambios,
            }
            sesion["cambio_limites"] = True
            sesion.setdefault("cambios_limites", []).append(registro)
            sesion["ultima_actividad"] = registro["fecha"]
            guardar_historial(data)
            return {"registrado": True, "cambios": cambios}

    return {"registrado": False, "cambios": cambios, "error": "sesion_invalida"}
