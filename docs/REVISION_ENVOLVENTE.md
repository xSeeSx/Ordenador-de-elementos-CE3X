# Revisión de envolvente para el nuevo ordenador CE3X

Fecha: 18/11/2026. Estado: decisiones de diseño y escenarios pendientes de ensayo, no implementación.

## Evidencia utilizada

- Manual de usuario incluido en el instalador oficial CE3X 2.3: zonas, p. 20; envolvente, pp. 28-53; sombras, pp. 59 y siguientes. Numeración impresa, no del visor PDF.
- Ampliación del manual de octubre de 2026, publicada por MITECO. No proporciona una especificación del formato CEX ni una matriz completa de compatibilidad de movimientos.
- Cuatro ejemplos oficiales residenciales, dos por versión. No incluyen todas las tipologías ni zonas anidadas.
- Inspección inerte de sus CEX, sin ejecutar código contenido en los archivos.

## Inventario que debe representar el árbol

Cubiertas sobre aire o terreno; muros de fachada, enterrados y medianerías; suelos sobre aire o terreno; particiones verticales y horizontales hacia espacios no habitables superiores o inferiores; huecos y lucernarios; puentes térmicos. Deben contemplarse zonas anidadas. Las sombras y las composiciones son referencias y propiedades, no nuevos padres creados por el ordenador.

En los ejemplos inspeccionados, los huecos guardan su asociación y orientación. Los puentes guardan un cerramiento, tipología, longitud y valor psi, sin un identificador explícito del hueco. Un nombre parecido no demuestra una relación.

El manual vincula los huecos a fachadas y cubiertas sobre aire, heredando su orientación. Indica superficies brutas y descuento interno de huecos. Permite asociar ciertos puentes a cualquiera de los dos cerramientos del encuentro y distingue valores por defecto y personalizados.

## Reglas propuestas

| Escenario | Respuesta del ordenador |
|---|---|
| Reordenar entre hermanos | Sin confirmación. Animación individual y hueco visible de inserción. |
| Reordenar un cerramiento | Su contenido lo acompaña; se mantiene su zona. |
| Soltar antes o después de una fila | Cambia la posición, sin cambiar padre. El indicador debe diferenciarlo de soltar dentro. |
| Soltar dentro de otro padre | Presentar el destino explícito antes de aceptar. No inferirlo por proximidad visual. |
| Arrastrar un grupo plegado | La tarjeta flotante muestra cuántos elementos contiene. Apertura por permanencia al pasar por un destino. |
| Cancelar con Escape, soltar fuera o perder captura | Restaurar el estado anterior completo, sin generar archivo. |
| Deshacer un cambio de padre | Una sola acción restaura asociación, orientación y todos los cambios aceptados en esa operación. |
| Hueco entre fachadas de igual orientación | Confirmación de cambio de asociación, con origen y destino. |
| Hueco entre fachadas de distinta orientación | Explicar el cambio de orientación requerido. Confirmar el conjunto o cancelar. No ofrecer guardar una orientación incoherente. |
| Hueco hacia cubierta sobre aire, o inverso | No tratar como una mera ordenación. Bloquear inicialmente hasta contrastar cambio hueco/lucernario y correcciones solares con muestras de ambas versiones. |
| Hueco hacia medianería, muro enterrado, suelo o partición | Bloquear el cambio de asociación. |
| Hueco con multiplicador | Mover el conjunto completo que representa la fila. Mostrarlo en el aviso; no dividir ni duplicar. |
| Superficie acristalada excesiva tras mover | Evaluar origen y destino con las superficies efectivas que usa CE3X. Bloquear un nuevo exceso en destino. Un defecto preexistente no debe impedir una mera reordenación. |
| Protección solar y patrón de sombras | Conservarlos al reordenar. Al cambiar padre, informar que requieren revisión. No sustituirlos por los de la fachada sin una decisión explícita. |
| Correcciones solares calculadas | No conservar como válidos factores dependientes de una orientación que se haya cambiado. Verificar cómo los recalcula CE3X antes de habilitar el guardado de ese movimiento. |
| Puente contorno de hueco o caja de persiana | No inventar una relación con una ventana a partir del nombre. El cambio de padre queda bloqueado si no se puede verificar su coherencia. |
| Hueco con posibles puentes relacionados | No arrastrar automáticamente los puentes ni dejarlos atrás silenciosamente. Mostrar los candidatos como no verificados y bloquear la reasociación hasta resolver la dependencia. |
| Puente fachada-cubierta | No rechazarlo solo por estar asociado a una cubierta; contemplar ambos lados del encuentro. |
| Puente fachada-suelo exterior o fachada-solera | Comprobar tipos compatibles y existencia de los elementos necesarios. La mera existencia de un suelo no demuestra el encuentro físico. |
| Encuentro físicamente desconocido | Avisar que el CEX no demuestra la adyacencia. No afirmar que no existe; bloquear la reasociación si falta información necesaria. |
| Puentes personalizados | Conservar psi, longitud y procedencia al ordenar. No regenerarlos con valores por defecto. |
| Posible duplicidad de puente | No añadir una copia al cambiar de padre. Avisar de coincidencias, sin borrar puentes existentes automáticamente. |
| Nombre duplicado o padre inexistente | Mostrar diagnóstico. No elegir el primer nombre coincidente ni reasignar al edificio automáticamente. |
| Zonas anidadas | Representar la jerarquía real. Impedir ciclos y soltar una zona dentro de sí misma o sus descendientes. |
| Cerramiento que cambia de zona | Operación semántica distinta de ordenar. Mantener bloqueada hasta contrastar la propagación a huecos, puentes y otras referencias. |
| Medidas de mejora con copias o referencias a elementos | No actualizar por coincidencia de nombres. Inspeccionar identificadores y copias internas antes de permitir un cambio semántico. |
| Archivo ya calculado | Solo ordenar conserva el modelo. Las reasociaciones requieren recalcular y deben invalidar la presentación de resultados anteriores de forma verificada. |
| Versión o uso no soportado | Rechazo explícito. La revisión del manual no amplía por sí sola la compatibilidad. |

## Decisiones de interfaz

- Estilo elegido: deslizamiento continuo, filas compactas e iconos que distingan cerramientos, huecos y puentes. La jerarquía debe reflejar relaciones reales.
- La indicación de posición y la de cambio de padre son distintas. Destino bloqueado con motivo visible antes de soltar.
- No abrir diálogos durante el movimiento. Confirmación al soltar si existe un cambio de asociación aceptable.
- Respetar reducción de movimiento del sistema y ofrecer teclado y deshacer.
- Las operaciones se mantienen en memoria. Generar escribe una copia solo tras validar el estado final.

## Comprobaciones necesarias antes de habilitar reasociaciones

1. Muestras guardadas por CE3X 2.3 y 3.2 con lucernarios, todos los tipos de cerramiento y zonas anidadas.
2. Un hueco repetido con multiplicador, sombras, lamas/retranqueo y doble ventana; comprobar qué significa exactamente superficie en cada estructura. No multiplicar dos veces un total ya almacenado.
3. Puentes de ambos lados de un encuentro, puentes personalizados, psi negativo justificado y longitudes corregidas. No declarar psi negativo inválido por defecto.
4. Huecos y puentes con nombres ambiguos, asociaciones huérfanas y datos antiguos incompletos.
5. Medidas de mejora con envolventes guardadas y UUID de 3.2: comprobar referencias y recalcular en CE3X.
6. Abrir y guardar el resultado en la versión de CE3X de origen. Comparar geometría, orientación, asociaciones, sombras y resultados. Una lectura con nuestro lector no sustituye esta prueba.
7. Arrastrar elementos con el árbol plegado, al primer/último lugar, fuera de la ventana y durante desplazamiento automático; comprobar cancelación y deshacer.

Los cuatro ejemplos oficiales actuales no cubren esta batería. No se marca como probada una regla por aparecer en esta tabla. La versión 0.1.0 no permite cambios de padre y se mantiene así durante esta revisión.
