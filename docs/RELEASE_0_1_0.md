Primera versión experimental para Windows 10/11 de 64 bits. Python incluido.

Descarga **Ordenador_CE3X_Windows_0_1_0.zip**, extrae todo el ZIP y abre **Ordenador_CE3X.exe**.

- Abre proyectos residenciales de CE3X 2.3 y 3.2.
- Reordena cerramientos, huecos y puentes térmicos mediante botones o arrastrando delante de otro elemento.
- Mantiene las asociaciones y permite deshacer y rehacer.
- Guarda una copia `_ORDENADO.cex`, conservando el original y su versión.
- Conserva byte a byte los 14 bloques ajenos a la envolvente y comprueba propiedades y referencias después de guardar.
- Interfaz nativa con el mismo estilo compacto del conversor.

Pruebas del motor y del ejecutable incluidas en GitHub Actions. Reordenación estructural ensayada localmente con los cuatro ejemplos oficiales residenciales de 2.3 y 3.2.

**Pendiente de comprobar visualmente dentro de CE3X**: orden del árbol y de los informes. No modifica resultados energéticos ni recalcula el certificado. Los huecos y puentes solo se ordenan dentro de su cerramiento. Rechaza estructuras desconocidas y cerramientos con nombres duplicados.

Los archivos "Source code" son el código fuente. El ZIP de Windows es el programa listo para usar.
