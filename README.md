# NeoSensa - Sistema de Monitoreo de Incubadoras Neonatales

Proyecto académico realizado en la carrera de Bioingeniería (ITBA), en el marco de la materia Instrumentación Biomédica II.

## Descripción

NeoSensa es un sistema de monitoreo de variables ambientales en incubadoras neonatales.

El proyecto propone una solución basada en sensores, ESP32 y una interfaz gráfica para visualizar datos en tiempo real, detectar alarmas y registrar mediciones.

## Objetivos

- Medir variables ambientales relevantes dentro de incubadoras
- Centralizar el monitoreo de múltiples incubadoras
  - Visualizar datos en tiempo real
  - Generar alertas ante condiciones fuera de rango
  - Registrar mediciones y eventos de alarma

## Variables monitoreadas

- Temperatura
- Humedad
- Oxígeno
- Iluminancia

## Tecnologías utilizadas

- ESP32
- Python
- JavaScript (Interfaz Gráfica de Usuario)
- MQTT
- CSV / XLSX

## Funcionalidades

- Supervisión centralizada de incubadoras
- Monitoreo en tiempo real
- Interfaz de control de variables de monitoreo
- Sistema de alertas
- Registro histórico de alarmas
- Exportación de datos

## Contenido del repositorio

- Código del sistema de monitoreo (scr/main.cpp)
- Simulación de datos y procesamiento (scripts/mqtt_simulacion2_csv.py)
- Implementación de adquisición, comunicación y visualización de datos (data/servidor_flask_csv.py)
- Desarrollo de la interfaz gráfica (data/XXX.html)

## Documentación adicional

- [Flujo software-hardware](Flowchart.md)

## Alcance

Este repositorio corresponde a un proyecto académico orientado al desarrollo de una solución biomédica, integrando hardware, sensores, comunicaciones y software.

## Autores

Proyecto realizado por Antonella Santi Kissner (Software) y Lola Erbin (Hardware).
