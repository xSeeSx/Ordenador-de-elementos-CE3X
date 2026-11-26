# Diseño de la interfaz 0.3.0

- [Atlassian: diseño de arrastre](https://atlassian.design/components/pragmatic-drag-and-drop/design-guidelines/): indicador de inserción y colocación relativa antes/después; no se permite combinar elementos.
- [Fluent: duración y suavizado](https://learn.microsoft.com/en-us/windows/apps/design/motion/timing-and-easing): transición corta de 167 ms con desaceleración; consulta de SPI_GETCLIENTAREAANIMATION.
- [W3C: alternativa al arrastre](https://www.w3.org/WAI/WCAG22/Understanding/dragging-movements): botones de movimiento sin arrastrar y navegación por teclado.

La vista nativa utiliza una lista Windows con nombres accesibles, selección y barra de desplazamiento, pintada con doble búfer. El árbol interno conserva la jerarquía y no es visible. Durante el arrastre solo se modifican coordenadas de previsualización; al soltar se envía un único movimiento al motor. Los grupos colapsados conservan todas sus asociaciones. El cambio de cerramiento está bloqueado en esta versión. Se requiere una revisión manual de comodidad, lectores de pantalla y certificados reales, además de la prueba automatizada de Windows.
