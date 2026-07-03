const AUTH_API_BASE = "http://localhost:5000";
const LIMITES_API_BASE = "http://localhost:5001";
const SESSION_STORAGE_KEY = "neosensa_sesion";

function obtenerSesionGuardada() {
    try {
        return JSON.parse(localStorage.getItem(SESSION_STORAGE_KEY));
    } catch (error) {
        return null;
    }
}

function limpiarSesionGuardada() {
    localStorage.removeItem(SESSION_STORAGE_KEY);
}

function guardarSesionLocal(sesion) {
    localStorage.setItem(SESSION_STORAGE_KEY, JSON.stringify(sesion));
}

async function verificarSesionActiva() {
    const sesion = obtenerSesionGuardada();
    if (!sesion || !sesion.session_id) return null;

    const response = await fetch(`${AUTH_API_BASE}/session`, {
        method: "POST",
        headers: {
            "Content-Type": "application/json",
            "X-Session-Id": sesion.session_id
        },
        body: JSON.stringify({ session_id: sesion.session_id })
    });

    if (!response.ok) return null;
    const data = await response.json();
    return data.ok ? { ...sesion, ...data } : null;
}

function redirigirALogin() {
    limpiarSesionGuardada();
    window.location.href = "login.html";
}

async function protegerSesion() {
    const sesion = await verificarSesionActiva();
    if (!sesion) {
        redirigirALogin();
        return null;
    }
    guardarSesionLocal(sesion);
    const etiquetasUsuario = document.querySelectorAll("[data-user-label]");
    etiquetasUsuario.forEach(el => {
        el.textContent = sesion.nombre || sesion.email;
    });
    return sesion;
}

async function cerrarSesion(event) {
    if (event) event.preventDefault();
    const sesion = obtenerSesionGuardada();

    if (sesion && sesion.session_id) {
        try {
            await fetch(`${AUTH_API_BASE}/logout`, {
                method: "POST",
                headers: {
                    "Content-Type": "application/json",
                    "X-Session-Id": sesion.session_id
                },
                body: JSON.stringify({ session_id: sesion.session_id })
            });
        } catch (error) {
            console.warn("No se pudo registrar el cierre de sesión:", error);
        }
    }

    redirigirALogin();
}

async function guardarLimitesAuditados(config) {
    const sesion = obtenerSesionGuardada();
    if (!sesion || !sesion.session_id) {
        throw new Error("No hay una sesión activa.");
    }

    const response = await fetch(`${LIMITES_API_BASE}/limites`, {
        method: "POST",
        headers: {
            "Content-Type": "application/json",
            "X-Session-Id": sesion.session_id
        },
        body: JSON.stringify({
            ...config,
            session_id: sesion.session_id
        })
    });

    const data = await response.json().catch(() => ({}));
    if (!response.ok || !data.ok) {
        throw new Error(data.error || "No se pudieron guardar los límites.");
    }
    return data;
}

document.addEventListener("DOMContentLoaded", () => {
    protegerSesion().catch(() => redirigirALogin());

    document.querySelectorAll('a[href="login.html"]').forEach(link => {
        if (link.textContent.includes("Cerrar sesión")) {
            link.addEventListener("click", cerrarSesion);
        }
    });

    setInterval(() => {
        verificarSesionActiva().catch(() => {});
    }, 60000);
});

let fecha_mqtt = null;
let tiempo_mqtt = null;
let tipo_mqtt = null;
let ultimaAlertaTemp = null;
let ultimaAlertaHum = null;
let ultimaAlertaLuz = null;
let ultimaAlertaOx = null;
let temperatura_mqtt = null;
let humedad_mqtt = null;
let iluminancia_mqtt = null;
let oxigeno_mqtt = null;
let parametro_mqtt = null;
let gaugeT = null;
let gaugeH = null;
let gaugeI = null;
let gaugeO = null;
let Tmin, Tmax, Hmin, Hmax, Imin, Imax, Omin, Omax;

function configurarBotonSilenciarAlarma(client, esp_id) {
    const boton = document.getElementById("botonSilenciarAlarma");
    if (!boton) return;

    boton.addEventListener("click", () => {
        if (!esp_id) {
            alert("No se pudo identificar la incubadora.");
            return;
        }
        if (!client.connected) {
            alert("No hay conexión con el broker MQTT. Esperá unos segundos e intentá nuevamente.");
            return;
        }

        client.publish(`incubadora/${esp_id}/silenciar_alarma`, JSON.stringify({
            esp: esp_id,
            accion: "silenciar_alarma"
        }));
        alert("Se envió la orden para silenciar la alarma durante 60 segundos.");
    });
}


function Fecha(fechaISO) {
    const fecha = new Date(fechaISO);
    const dia = String(fecha.getDate()).padStart(2, '0');
    const mes = String(fecha.getMonth() + 1).padStart(2, '0');
    const año = String(fecha.getFullYear());
    return `${dia}/${mes}/${año}`;
}


function actualizarDato(selector, nuevoValor, sufijo = " ") {
    if (nuevoValor !== undefined) {
        document.querySelector(selector).textContent = nuevoValor + sufijo;
    }
}


function actualizarLeyenda(selector, nuevoValor, Parametro = " ") {
    document.querySelector(selector).textContent = Parametro + nuevoValor;
}

function decimalesParametro(parametro = "") {
    return ["H", "O"].includes(parametro) ? 0 : 2;
}

function formatearValorParametro(valor, parametro = "") {
    const numero = parseFloat(valor);
    if (!Number.isFinite(numero)) return valor;
    return numero.toFixed(decimalesParametro(parametro));
}

function parametroDesdeHistorial(historialParametro = "") {
    const partes = historialParametro.split("_");
    return partes[partes.length - 1] || "";
}

const GRAFICO_VENTANA_MS = 60 * 60 * 1000;
let graficoVentanaOffset = 0;

function fechaGraficoMs(fecha) {
    const valor = new Date(fecha).getTime();
    return Number.isFinite(valor) ? valor : null;
}

function formatearHoraVentana(ms) {
    return new Date(ms).toLocaleTimeString([], {
        hour: "2-digit",
        minute: "2-digit"
    });
}

function obtenerTiemposGrafico(chart) {
    const puntos = chart.data.datasets[0].data || [];
    return puntos
        .map(p => fechaGraficoMs(p.x))
        .filter(t => t !== null);
}

function obtenerExtremoDerechoGrafico(chart) {
    const tiempos = obtenerTiemposGrafico(chart);

    if (tiempos.length === 0) return Date.now();
    return Math.max(...tiempos);
}

function actualizarEtiquetaVentanaGrafico(chart, inicio, fin) {
    const contenedor = chart.canvas.parentElement;
    const etiqueta = contenedor ? contenedor.querySelector("[data-grafico-ventana]") : null;
    if (!etiqueta) return;

    etiqueta.textContent = `${formatearHoraVentana(inicio)} - ${formatearHoraVentana(fin)}`;
}

function aplicarVentanaGrafico(chart) {
    const tiempos = obtenerTiemposGrafico(chart);
    const ultimo = obtenerExtremoDerechoGrafico(chart);
    const primero = tiempos.length > 0 ? Math.min(...tiempos) : ultimo;
    const maxOffset = Math.max(0, Math.ceil((ultimo - primero) / GRAFICO_VENTANA_MS) - 1);
    graficoVentanaOffset = Math.min(graficoVentanaOffset, maxOffset);

    const fin = ultimo - (graficoVentanaOffset * GRAFICO_VENTANA_MS);
    const inicio = Math.max(primero, fin - GRAFICO_VENTANA_MS);

    chart.options.scales.x.min = inicio;
    chart.options.scales.x.max = fin;
    actualizarEtiquetaVentanaGrafico(chart, inicio, fin);
    chart.update();

    const contenedor = chart.canvas.parentElement;
    if (!contenedor) return;

    const btnAdelante = contenedor.querySelector("[data-grafico-adelante]");
    if (btnAdelante) btnAdelante.disabled = graficoVentanaOffset === 0;

    const btnAtras = contenedor.querySelector("[data-grafico-atras]");
    if (btnAtras) btnAtras.disabled = graficoVentanaOffset >= maxOffset;
}

function configurarNavegacionGrafico(chart) {
    const contenedor = chart.canvas.parentElement;
    if (!contenedor || contenedor.querySelector(".grafico-controles")) return;

    const controles = document.createElement("div");
    controles.className = "grafico-controles";
    controles.innerHTML = `
        <button type="button" class="btn btn-mini" data-grafico-atras title="Ver hora anterior">←</button>
        <span data-grafico-ventana>--:-- - --:--</span>
        <button type="button" class="btn btn-mini" data-grafico-adelante title="Ver hora siguiente">→</button>
    `;

    const encabezado = contenedor.querySelector(".block-header");
    if (encabezado) {
        encabezado.appendChild(controles);
    } else {
        contenedor.insertBefore(controles, chart.canvas);
    }

    controles.querySelector("[data-grafico-atras]").addEventListener("click", () => {
        graficoVentanaOffset += 1;
        aplicarVentanaGrafico(chart);
    });

    controles.querySelector("[data-grafico-adelante]").addEventListener("click", () => {
        graficoVentanaOffset = Math.max(0, graficoVentanaOffset - 1);
        aplicarVentanaGrafico(chart);
    });
}


function calcularDiferencia(tiempoAlerta) {
    const diffMs = Date.now() - tiempoAlerta;
    const segundos = Math.floor(diffMs / 1000);
    const minutos = Math.floor(segundos / 60);
    const horas = Math.floor(minutos / 60);

    if (horas > 0) return `hace ${horas}h ${minutos % 60}m`;
    if (minutos > 0) return `hace ${minutos}m ${segundos % 60}s`;
    if (segundos >= 2) return `Hace ${segundos}s`;
    else if (segundos < 2) return "Actual";
}


async function obtenerHistorial(esp_id) {
    const response = await fetch("http://localhost:5000/datos");
    const data = await response.json();
    const info = data[`incubadora_${esp_id}`]
    const historial_T = info.historial_T;
    const historial_H = info.historial_H;
    const historial_I = info.historial_I;
    const historial_O = info.historial_O;
    return {
        historial_T: historial_T,
        historial_H: historial_H,
        historial_I: historial_I,
        historial_O: historial_O
    }
}

async function obtenerDatos(esp_id) {
    const response = await fetch("http://localhost:5000/datos");
    const data = await response.json();
    const info = data[`incubadora_${esp_id}`]
    const dato_T = info.T_actual;
    const dato_H = info.H_actual;
    const dato_I = info.I_actual;
    const dato_O = info.O_actual;
    return {
        dato_T: dato_T,
        dato_H: dato_H,
        dato_I: dato_I,
        dato_O: dato_O,
    }
}

function completarTabla_home(Parametro = "", datos, esp_id, unidad = "") {
    if (datos !== null && datos !== undefined && datos !== "") {
        const valor = formatearValorParametro(datos, Parametro);
        document.getElementById(`${Parametro}_home_${esp_id}`).textContent = valor + unidad;
    }
}

async function iniciarTabla_home(esp_id1, esp_id2) {
    const datos1 = await obtenerDatos(esp_id1);
    const datos2 = await obtenerDatos(esp_id2);
    completarTabla_home("T", datos1.dato_T, esp_id1, " °C");
    completarTabla_home("H", datos1.dato_H, esp_id1, " %");
    completarTabla_home("I", datos1.dato_I, esp_id1, " lux");
    completarTabla_home("O", datos1.dato_O, esp_id1, " %");
    completarTabla_home("T", datos2.dato_T, esp_id2, " °C");
    completarTabla_home("H", datos2.dato_H, esp_id2, " %");
    completarTabla_home("I", datos2.dato_I, esp_id2, " lux");
    completarTabla_home("O", datos2.dato_O, esp_id2, " %");
}

function completarTabla_gral(Parametro = "", historial) {
    if (historial && historial.length !== 0) {
        document.getElementById(`ultima_${ Parametro }`).textContent = calcularDiferencia(new Date(historial[0].fecha).getTime());
        document.getElementById(`fecha_${ Parametro }`).textContent = Fecha(historial[0].fecha);
        document.getElementById(`hora_${ Parametro }`).textContent = new Date(historial[0].fecha).toLocaleTimeString();
    }
}

async function iniciarTabla_gral(esp_id) {
    const hist = await obtenerHistorial(esp_id);
    completarTabla_gral("T", hist.historial_T);
    completarTabla_gral("H", hist.historial_H);
    completarTabla_gral("I", hist.historial_I);
    completarTabla_gral("O", hist.historial_O);
}


//////////////////////////////////////////////////////////////////////////////////

async function actualizarTiemposAlertas(esp_id) {
    const hist = await obtenerHistorial(esp_id);
    if (ultimaAlertaTemp) {
        actualizarDato("#ultima_T", calcularDiferencia(ultimaAlertaTemp), "  ");
    } else {
        if (hist.historial_T && hist.historial_T.length !== 0) {
            actualizarDato("#ultima_T", calcularDiferencia(new Date(hist.historial_T[0].fecha).getTime()), "  ");
        }
    }
    if (ultimaAlertaHum) {
        actualizarDato("#ultima_H", calcularDiferencia(ultimaAlertaHum), "  ");
    } else {
        if (hist.historial_H && hist.historial_H.length !== 0) {
            actualizarDato("#ultima_H", calcularDiferencia(new Date(hist.historial_H[0].fecha).getTime()), "  ");
        }
    }
    if (ultimaAlertaLuz) {
        actualizarDato("#ultima_I", calcularDiferencia(ultimaAlertaLuz), "  ");
    } else {
        if (hist.historial_I && hist.historial_I.length !== 0) {
            actualizarDato("#ultima_I", calcularDiferencia(new Date(hist.historial_I[0].fecha).getTime()), "  ");
        }
    }
    if (ultimaAlertaOx) {
        actualizarDato("#ultima_O", calcularDiferencia(ultimaAlertaOx), "  ");
    } else {
        if (hist.historial_O && hist.historial_O.length !== 0) {
            actualizarDato("#ultima_O", calcularDiferencia(new Date(hist.historial_O[0].fecha).getTime()), "  ");
        }
    }
}

async function actualizarGrafico(esp_id, datos_parametro = "", parametro_grafico = "", parametro_mqtt, fecha_mqtt) {
    try {
        const response = await fetch("http://localhost:5000/datos");
        const data = await response.json();
        const info = data[`incubadora_${esp_id}`]
        let datosParametro = info[datos_parametro].map(d => ({
            x: d.fecha_grafico,
            y: d[parametro_grafico]
        }));

        const puntoMqttYaExiste = datosParametro.some(p => p.x === fecha_mqtt);
        if (fecha_mqtt && parametro_mqtt != null && !puntoMqttYaExiste) {
            datosParametro.push({
                x: fecha_mqtt,
                y: parametro_mqtt
            });
        }

        // Color según el parámetro
        let colorBase = "red";
        if (parametro_grafico === "temperatura_grafico") colorBase = "red";
        else if (parametro_grafico === "humedad_grafico") colorBase = "blue";
        else if (parametro_grafico === "iluminancia_grafico") colorBase = "orange";
        else if (parametro_grafico === "oxigeno_grafico") colorBase = "gray";

        const colores = datosParametro.map((_, i) =>
            i === datosParametro.length - 1 ? "black" : colorBase
        );

        chart.data.datasets[0].data = datosParametro;
        chart.data.datasets[0].pointBackgroundColor = colores;
        configurarNavegacionGrafico(chart);
        aplicarVentanaGrafico(chart);
        console.log("📈 Actualizando gráfico con", datosParametro.length, "puntos");

    } catch (error) {
        console.error("❌ Error al cargar datos:", error);
    }
}

async function actualizarHistorial(esp_id, historial_parametro = "", parametro_mqtt, fecha_mqtt, tipo_mqtt, unidad = "") { // Carga máximo 14 filas(distintos en cada html)
    const response = await fetch("http://localhost:5000/datos");
    const data = await response.json();
    const info = data[`incubadora_${esp_id}`]
    const historial = info[historial_parametro];
    const parametro = parametroDesdeHistorial(historial_parametro);

    if (tipo_mqtt !== null && tipo_mqtt !== "-" && parametro_mqtt !== null && fecha_mqtt !== null) { // Si llega una alarma por MQTT
        document.getElementById("valor_1").textContent = formatearValorParametro(parametro_mqtt, parametro) + unidad;
        document.getElementById("tipo_1").textContent = tipo_mqtt;
        document.getElementById("fecha_1").textContent = fecha_mqtt.replace("T", " ");

        for (let i = 1; i < Math.min(historial.length, 14); i++) {
            document.getElementById(`valor_${ i + 1 }`).textContent = formatearValorParametro(historial[i].valor, parametro) + unidad;
            document.getElementById(`tipo_${ i + 1 }`).textContent = historial[i].tipo
            document.getElementById(`fecha_${ i + 1 }`).textContent = historial[i].fecha.replace("T", " ");
        }
    } else {
        for (let i = 0; i < Math.min(historial.length, 14); i++) {
            document.getElementById(`valor_${ i + 1 }`).textContent = formatearValorParametro(historial[i].valor, parametro) + unidad;
            document.getElementById(`tipo_${ i + 1 }`).textContent = historial[i].tipo;
            document.getElementById(`fecha_${ i + 1 }`).textContent = historial[i].fecha.replace("T", " ");
        }
    }
}

function actualizarAlertas(alarma_Parametro_mqtt, fecha_mqtt, fila_Parametro = "", fecha_Parametro = "") {
    if (alarma_Parametro_mqtt === true) {
        actualizarDato(fecha_Parametro, fecha_mqtt.replace("T", " "), " ");
        document.getElementById(fila_Parametro).style.color = "#b94a48";
        document.getElementById(fila_Parametro).style.fontWeight = "bold";
    } else {
        document.getElementById(fila_Parametro).style.color = "black";
        document.getElementById(fila_Parametro).style.fontWeight = "normal";
    }
}

function completarTabla_parametro(Parametro = "", historial) {
    if (!historial || historial.length === 0) return;
    else document.getElementById(`fecha_${ Parametro }`).textContent = (historial[0].fecha).replace("T", " ");
}

async function iniciarTabla_Parametro(esp_id) {
    const hist = await obtenerHistorial(esp_id);
    completarTabla_parametro("T", hist.historial_T);
    completarTabla_parametro("H", hist.historial_H);
    completarTabla_parametro("I", hist.historial_I);
    completarTabla_parametro("O", hist.historial_O);
}

async function iniciarEstadistica_Parametro(esp_id, historial_Parametro = "", parametro = "", Letra = "", unidad = "") {
    const response = await fetch("http://localhost:5000/datos");
    const data = await response.json();
    const info = data[`incubadora_${esp_id}`]
    const estadisticas = info.estadisticas[parametro];
    const media = estadisticas.media;
    const mediana = estadisticas.mediana;
    const minimo = estadisticas.min;
    const maximo = estadisticas.max;
    const desvio = estadisticas.desvio;
    const hist = info[historial_Parametro]

    if (estadisticas && estadisticas.length !== 0) {
        document.getElementById(`N_${ Letra }`).textContent = hist.length;
        document.getElementById(`min_${ Letra }`).textContent = formatearValorParametro(minimo, Letra) + unidad;
        document.getElementById(`max_${ Letra }`).textContent = formatearValorParametro(maximo, Letra) + unidad;
        document.getElementById(`media_${ Letra }`).textContent = formatearValorParametro(media, Letra) + unidad;
        document.getElementById(`mediana_${ Letra }`).textContent = formatearValorParametro(mediana, Letra) + unidad;
        document.getElementById(`desvio_${ Letra }`).textContent = formatearValorParametro(desvio, Letra) + unidad;
    }
}

function getParametroESP() {
    const urlParams = new URLSearchParams(window.location.search);
    return urlParams.get('esp'); // devuelve "1" o "2"
}

function gauges(parametro = "", Min, Max, lim_inf, lim_sup, tolerancia) {
    const rango = Max - Min;
    const decimalesEtiqueta = rango <= 10 ? 2 : 0;

    // Calcular puntos de color
    const LI = lim_inf;
    const LS = lim_sup;
    let LI_tol = null;
    if (parametro !== "O2") {
        LI_tol = LI - tolerancia
    } else { LI_tol = LI }
    const LS_tol = LS + tolerancia;

    // Construir opciones
    var opts = {
        // color configs
        colorStart: "#6fadcf",
        colorStop: void 0,
        gradientType: 0,
        strokeColor: "#e0e0e0",
        generateGradient: true,
        percentColors: [
            [0.0, "#a9d70b"],
            [0.50, "#f9c802"],
            [1.0, "#ff0000"]
        ],
        // customize pointer
        pointer: {
            length: 0.8,
            strokeWidth: 0.035,
            iconScale: 1.0
        },
        // static labels
        staticLabels: {
            font: "10px sans-serif",
            labels: [Min, lim_inf, lim_sup, Max],
            fractionDigits: decimalesEtiqueta
        },
        // static zones
        staticZones: [{
            strokeStyle: "#F03E3E",
            min: Min,
            max: LI_tol
        }, {
            strokeStyle: "#FFDD00",
            min: LI_tol,
            max: LI
        }, {
            strokeStyle: "#30B32D",
            min: LI,
            max: LS
        }, {
            strokeStyle: "#FFDD00",
            min: LS,
            max: LS_tol
        }, {
            strokeStyle: "#F03E3E",
            min: LS_tol,
            max: Max
        }],
        // render ticks
        renderTicks: {
            divisions: 0,
            divWidth: 1.1,
            divLength: 0.7,
        },
        // the span of the gauge arc
        angle: 0.1,
        // line thickness
        lineWidth: 0.44,
        // radius scale
        radiusScale: 1.0,
        // font size
        fontSize: 40,
        // if false, max value increases automatically if value > maxValue
        limitMax: true,
        // if true, the min value of the gauge will be fixed
        limitMin: true,
        // High resolution support
        highDpiSupport: true
    };

    // Obtener el canvas y crear/actualizar el gauge
    const canvas = document.getElementById(parametro + 'Canvas');
    if (!canvas) {
        console.error(`No existe <canvas id="${parametro}Canvas">`);
        return null;
    }

    // Guardamos la instancia en la propiedad ._gauge para futuras actualizaciones
    let g = canvas._gauge;
    if (!g) {
        g = new Gauge(canvas).setOptions(opts);
        canvas._gauge = g;
    } else {
        g.setOptions(opts);
    }

    // Ajustar rango y animación
    g.maxValue = Max;
    g.setMinValue(Min);
    g.animationSpeed = 32;
    // Valor inicial en el mínimo
    g.set(Min);

    return g;
}
