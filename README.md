# Ordenador de elementos CE3X

Herramienta para reordenar cerramientos, huecos y puentes térmicos en archivos CE3X, conservando sus datos y asociaciones. Incluye la avanzada tecnología de "subir y bajar elementos", a la espera de que Efinovatic la descubra y la Junta la homologue.

## Descargar y usar

[Versiones para Windows y notas de cambios](https://github.com/xSeeSx/Ordenador-de-elementos-CE3X/releases).

Descarga el ZIP de Windows desde Releases, extrae todo su contenido y abre `Ordenador_CE3X.exe`. Incluye Python, no necesitas instalarlo. Windows 10/11 de 64 bits.

1. Abre un `.cex` residencial de CE3X 2.3 o 3.2.
2. Selecciona un cerramiento, un hueco o un puente térmico. Usa Subir, Bajar, Al inicio, Al final o arrástralo antes o después de otro elemento compatible.
3. Pulsa Guardar copia. Se propone `nombre_ORDENADO.cex` en la carpeta de origen.

Al mover un cerramiento, sus huecos y puentes lo acompañan. Dentro de cada cerramiento puedes ordenar los huecos y los puentes por separado. No permite cambiarlos de cerramiento.

Deshacer `Ctrl+Z`, rehacer `Ctrl+Y`, abrir `Ctrl+O`, guardar copia `Ctrl+S`. También puedes abrir un archivo arrastrándolo a la ventana. Ventana redimensionable con el mismo estilo técnico compacto del [conversor CE3X](https://github.com/xSeeSx/Conversor-CE3X-3.2-a-CE3X-2.3).

## Interfaz de ordenación

La lista muestra filas compactas con agarres, jerarquía visual y grupos plegables. Al arrastrar, las filas vecinas se deslizan dejando un espacio de inserción. Los cerramientos arrastran todo su grupo, incluidos los elementos ocultos al plegarlo. Puedes soltar antes o después, cancelar con Esc y desplazarte automáticamente cerca de los bordes. Los destinos incompatibles se bloquean; el archivo no cambia hasta que guardas la copia.

Los botones siguen disponibles sin arrastrar. Ctrl + flechas arriba/abajo mueve la selección; las flechas izquierda/derecha permiten plegar o desplegar grupos. La animación de 167 ms respeta la configuración de animaciones de Windows.

## Actualizaciones

La aplicación consulta GitHub al abrirse. También puedes pulsar **Buscar actualizaciones**. Si hay una versión posterior con paquete de Windows publicado, aparece un botón de descarga. La consulta no bloquea la ordenación ni envía certificados, rutas ni datos del usuario. Descarga el ZIP nuevo y extráelo en otra carpeta. Sin conexión, el ordenador sigue funcionando.

Los errores explican qué acción falló y, cuando se conoce, qué elemento está afectado. Si no se reconoce una estructura o falla la comprobación de seguridad, no se guarda la copia.

## Qué conserva

Mantiene la versión del archivo, todas las propiedades de los elementos y sus asociaciones. Solo cambia el orden de las listas de la envolvente. Los otros 14 bloques del CEX se conservan byte a byte, incluidas instalaciones, mejoras, sombras e informes. El original no se sobrescribe. No recalcula el certificado ni genera logs automáticamente.

**Versión 0.3.0, todavía experimental.** Se han ensayado la lectura, reordenación y conservación estructural con muestras sintéticas y ejemplos oficiales residenciales de ambas versiones. Queda pendiente comprobar visualmente los resultados dentro de CE3X y su informe. El programa rechaza estructuras desconocidas y nombres de cerramientos duplicados, porque harían ambiguas las asociaciones.

Si encuentras un fallo, [abre una incidencia](https://github.com/xSeeSx/Ordenador-de-elementos-CE3X/issues). Describe la versión de CE3X, el movimiento realizado y el resultado. Evita adjuntar proyectos con datos personales sin revisarlos.

## Desarrollo

Motor en Python estándar e interfaz nativa de Windows en C, sin dependencia del repositorio del conversor. Lector de pickle inerte: no importa módulos de CE3X ni ejecuta funciones serializadas.

Pruebas: `python -B -m unittest discover -s fuentes -v`.

Para compilar en Windows: Zig 0.13.0 en el PATH y `./fuentes/compilar_windows.ps1`. Coloca Python 3.11.9 embebido de 64 bits en `runtime/`. El flujo de GitHub Actions compila, prueba el motor con ambos Python, comprueba la comunicación con el EXE, captura la ventana y genera el ZIP mediante `python -B scripts/empaquetar.py`.

Licencia del runtime de Python incluida en `runtime/LICENSE.txt` del ZIP.
