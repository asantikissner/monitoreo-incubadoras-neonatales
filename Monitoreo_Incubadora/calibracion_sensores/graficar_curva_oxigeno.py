from html import escape


# (voltaje_min, voltaje_max, oxigeno_porcentaje)
MEDICIONES = [
    (2.296, 2.344, 47.0),
    (2.010, 2.079, 43.0),
    (1.951, 1.982, 39.0),
    (1.706, 1.720, 36.0),
    (0.907, 0.907, 21.0),
]


def punto_medio(v_min, v_max):
    return (v_min + v_max) / 2.0


def ajuste_lineal(puntos):
    xs = [p[0] for p in puntos]
    ys = [p[1] for p in puntos]
    x_prom = sum(xs) / len(xs)
    y_prom = sum(ys) / len(ys)

    numerador = sum((x - x_prom) * (y - y_prom) for x, y in puntos)
    denominador = sum((x - x_prom) ** 2 for x in xs)
    pendiente = numerador / denominador
    ordenada = y_prom - pendiente * x_prom

    predichos = [pendiente * x + ordenada for x in xs]
    ss_res = sum((y - y_pred) ** 2 for y, y_pred in zip(ys, predichos))
    ss_tot = sum((y - y_prom) ** 2 for y in ys)
    r2 = 1.0 - (ss_res / ss_tot)

    return pendiente, ordenada, r2


def calibrar_oxigeno_porcentaje(voltaje):
    pendiente = 18.39647163
    ordenada = 4.26663649
    oxigeno = pendiente * voltaje + ordenada
    return max(0.0, min(100.0, oxigeno))


def svg_text(x, y, texto, size=14, anchor="middle", weight="400", color="#222222"):
    return (
        f'<text x="{x:.1f}" y="{y:.1f}" font-size="{size}" '
        f'font-family="Arial, sans-serif" text-anchor="{anchor}" '
        f'font-weight="{weight}" fill="{color}">{escape(texto)}</text>'
    )


def main():
    puntos = [(punto_medio(v_min, v_max), oxigeno) for v_min, v_max, oxigeno in MEDICIONES]
    pendiente, ordenada, r2 = ajuste_lineal(puntos)

    ancho = 1100
    alto = 700
    margen_izq = 95
    margen_der = 45
    margen_sup = 80
    margen_inf = 85
    graf_ancho = ancho - margen_izq - margen_der
    graf_alto = alto - margen_sup - margen_inf

    x_min = 0.75
    x_max = 2.45
    y_min = 18.0
    y_max = 50.0

    def sx(x):
        return margen_izq + (x - x_min) / (x_max - x_min) * graf_ancho

    def sy(y):
        return margen_sup + (y_max - y) / (y_max - y_min) * graf_alto

    elementos = []
    elementos.append(
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{ancho}" height="{alto}" '
        f'viewBox="0 0 {ancho} {alto}">'
    )
    elementos.append('<rect width="100%" height="100%" fill="#ffffff"/>')
    elementos.append(svg_text(ancho / 2, 38, "Curva de calibracion del sensor de oxigeno", 24, weight="700"))

    # Grid y marcas
    for y in [20, 25, 30, 35, 40, 45, 50]:
        py = sy(y)
        elementos.append(
            f'<line x1="{margen_izq}" y1="{py:.1f}" x2="{margen_izq + graf_ancho}" '
            f'y2="{py:.1f}" stroke="#d9d9d9" stroke-width="1" stroke-dasharray="5 5"/>'
        )
        elementos.append(svg_text(margen_izq - 14, py + 5, f"{y}", 13, anchor="end", color="#444444"))

    for x in [0.8, 1.0, 1.2, 1.4, 1.6, 1.8, 2.0, 2.2, 2.4]:
        px = sx(x)
        elementos.append(
            f'<line x1="{px:.1f}" y1="{margen_sup}" x2="{px:.1f}" '
            f'y2="{margen_sup + graf_alto}" stroke="#eeeeee" stroke-width="1"/>'
        )
        elementos.append(svg_text(px, margen_sup + graf_alto + 28, f"{x:.1f}", 13, color="#444444"))

    # Ejes
    elementos.append(
        f'<line x1="{margen_izq}" y1="{margen_sup + graf_alto}" '
        f'x2="{margen_izq + graf_ancho}" y2="{margen_sup + graf_alto}" stroke="#222222" stroke-width="2"/>'
    )
    elementos.append(
        f'<line x1="{margen_izq}" y1="{margen_sup}" '
        f'x2="{margen_izq}" y2="{margen_sup + graf_alto}" stroke="#222222" stroke-width="2"/>'
    )

    # Recta de ajuste
    x1 = x_min
    y1 = pendiente * x1 + ordenada
    x2 = x_max
    y2 = pendiente * x2 + ordenada
    elementos.append(
        f'<line x1="{sx(x1):.1f}" y1="{sy(y1):.1f}" x2="{sx(x2):.1f}" y2="{sy(y2):.1f}" '
        f'stroke="#1f77b4" stroke-width="4"/>'
    )

    # Mediciones y rangos
    for v_min, v_max, oxigeno in MEDICIONES:
        v_mid = punto_medio(v_min, v_max)
        px = sx(v_mid)
        py = sy(oxigeno)
        px_min = sx(v_min)
        px_max = sx(v_max)

        elementos.append(
            f'<line x1="{px_min:.1f}" y1="{py:.1f}" x2="{px_max:.1f}" y2="{py:.1f}" '
            f'stroke="#d62728" stroke-width="3"/>'
        )
        elementos.append(
            f'<line x1="{px_min:.1f}" y1="{py - 8:.1f}" x2="{px_min:.1f}" y2="{py + 8:.1f}" '
            f'stroke="#d62728" stroke-width="2"/>'
        )
        elementos.append(
            f'<line x1="{px_max:.1f}" y1="{py - 8:.1f}" x2="{px_max:.1f}" y2="{py + 8:.1f}" '
            f'stroke="#d62728" stroke-width="2"/>'
        )
        elementos.append(f'<circle cx="{px:.1f}" cy="{py:.1f}" r="7" fill="#d62728" stroke="#ffffff" stroke-width="2"/>')

        y_label = py - 15 if oxigeno >= 36 else py - 18
        elementos.append(svg_text(px, y_label, f"{v_mid:.3f} V / {oxigeno:.0f}%", 12, color="#333333"))

    # Leyenda y ecuacion
    caja_x = margen_izq + 25
    caja_y = margen_sup + 22
    elementos.append(
        f'<rect x="{caja_x}" y="{caja_y}" width="355" height="95" rx="8" '
        f'fill="#ffffff" stroke="#bdbdbd" stroke-width="1.2"/>'
    )
    elementos.append(svg_text(caja_x + 18, caja_y + 30, f"O2 (%) = {pendiente:.4f} * V + {ordenada:.4f}", 15, anchor="start"))
    elementos.append(svg_text(caja_x + 18, caja_y + 56, f"R2 = {r2:.4f}", 15, anchor="start"))
    elementos.append(svg_text(caja_x + 18, caja_y + 82, "Puntos: media de cada rango de voltaje", 13, anchor="start", color="#555555"))

    elementos.append(svg_text(ancho / 2, alto - 25, "Voltaje medido (V)", 16, weight="700"))
    elementos.append(
        f'<text x="28" y="{alto / 2:.1f}" font-size="16" font-family="Arial, sans-serif" '
        f'text-anchor="middle" font-weight="700" fill="#222222" '
        f'transform="rotate(-90 28 {alto / 2:.1f})">Oxigeno (%)</text>'
    )

    elementos.append("</svg>")

    salida = "curva_calibracion_oxigeno.svg"
    with open(salida, "w", encoding="utf-8") as archivo:
        archivo.write("\n".join(elementos))

    print(f"Grafico guardado en {salida}")
    print(f"O2 (%) = {pendiente:.4f} * V + {ordenada:.4f}")
    print(f"R2 = {r2:.4f}")


if __name__ == "__main__":
    main()
