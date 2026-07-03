import matplotlib.pyplot as plt


PUNTOS_CALIBRACION = [
    (0.142, 0.000),
    (0.351, 0.040),
    (2.780, 0.097),
    (2.800, 0.117),
    (3.050, 0.170),
    (3.160, 0.410),
    (3.600, 0.520),
]


def calibrar_luz_uw_cm2(voltaje):
    if voltaje <= PUNTOS_CALIBRACION[0][0]:
        return PUNTOS_CALIBRACION[0][1]

    if voltaje >= PUNTOS_CALIBRACION[-1][0]:
        return PUNTOS_CALIBRACION[-1][1]

    for i in range(1, len(PUNTOS_CALIBRACION)):
        v0, y0 = PUNTOS_CALIBRACION[i - 1]
        v1, y1 = PUNTOS_CALIBRACION[i]
        if voltaje <= v1:
            proporcion = (voltaje - v0) / (v1 - v0)
            return y0 + proporcion * (y1 - y0)

    return PUNTOS_CALIBRACION[-1][1]


def main():
    voltajes = [p[0] for p in PUNTOS_CALIBRACION]
    irradiancias = [p[1] for p in PUNTOS_CALIBRACION]
    offsets = {
        0.142: (0, 8),
        0.351: (0, 8),
        2.780: (-30, 10),
        2.800: (30, -6),
        3.050: (0, 10),
        3.160: (0, 10),
        3.600: (-12, 8),
    }

    x_min = 0.0
    x_max = 3.8
    pasos = 500
    curva_x = [x_min + i * (x_max - x_min) / (pasos - 1) for i in range(pasos)]
    curva_y = [calibrar_luz_uw_cm2(x) for x in curva_x]

    fig, ax = plt.subplots(figsize=(9, 5.5), dpi=160)

    ax.plot(curva_x, curva_y, color="#1f77b4", linewidth=2.2, label="Interpolacion por tramos")
    ax.scatter(voltajes, irradiancias, color="#d62728", s=42, zorder=3, label="Mediciones")

    for v, y in PUNTOS_CALIBRACION:
        ax.annotate(
            f"{v:.3f} V\n{y:.3f}",
            (v, y),
            textcoords="offset points",
            xytext=offsets.get(v, (0, 8)),
            ha="center",
            fontsize=8,
        )

    ax.axvline(3.600, color="#7f7f7f", linestyle="--", linewidth=1.2)
    ax.axhline(0.520, color="#7f7f7f", linestyle=":", linewidth=1.2)
    ax.fill_between([3.600, x_max], [0.520, 0.520], [0.62, 0.62], color="#d9d9d9", alpha=0.45)
    ax.text(3.67, 0.590, "Zona saturada", fontsize=9, color="#555555", va="center")

    ax.set_title("Curva de calibracion del sensor de luz")
    ax.set_xlabel("Voltaje del sensor (V)")
    ax.set_ylabel("Irradiancia calibrada (uW/cm2)")
    ax.set_xlim(x_min, x_max)
    ax.set_ylim(-0.02, 0.62)
    ax.grid(True, linestyle="--", linewidth=0.6, alpha=0.45)
    ax.legend(loc="upper left")

    fig.tight_layout()
    fig.savefig("curva_calibracion_luz.png")
    print("Grafico guardado en curva_calibracion_luz.png")


if __name__ == "__main__":
    main()
