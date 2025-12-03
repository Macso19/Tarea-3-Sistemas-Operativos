# Simulador de Paginación con Reemplazo LRU (C++)

Este proyecto implementa un simulador visual de administración de memoria, utilizando paginación, memoria virtual, SWAP, page faults y el algoritmo de reemplazo LRU (Least Recently Used).
El sistema crea procesos, accede a páginas aleatorias y mata procesos, todo de forma concurrente mediante hilos (std::thread).

Incluye barras visuales de uso de RAM/SWAP y mensajes coloreados en consola para facilitar la comprensión del flujo de la memoria.
 Características principales

Simulación completa de RAM, Memoria Virtual y SWAP

Manejo de page tables por proceso

Algoritmo de reemplazo de página LRU

Page faults con swap-in y swap-out

Creación y eliminación aleatoria de procesos

Acceso aleatorio a páginas

Visualización del uso de RAM y SWAP

Concurrencia usando threads

# ¿Cómo funciona?

El programa:

Solicita configuración del usuario

Tamaño de la RAM

Tamaño de página

Tamaño mínimo y máximo de los procesos

Genera automáticamente memoria virtual, mayor que la RAM.

Crea hilos concurrentes:

 Tiempo: incrementa el reloj global

 Creador de procesos: genera procesos periódicamente

 Killer: elimina un proceso después de cierto tiempo

 Acceso a memoria: solicita páginas aleatorias provocando hits o page faults

Simula page faults mediante:

Búsqueda de frame libre

Reemplazo de página por LRU

Swap-in y swap-out

Muestra en consola el estado de RAM/SWAP usando barras visuales.
# Compilación y ejecución

Requiere C++17 o superior.

Compilar:
g++ -std=c++17

Ejecutar:
./paginacion.cpp dentro de la carpeta donde esta el archivo

 Parámetros de entrada

Durante la ejecución se solicitarán:

Tamaño de la RAM (MB)

Tamaño de página (KB)

Tamaño mínimo de proceso (MB)

Tamaño máximo de proceso (MB)

El sistema genera memoria virtual automáticamente basada en un factor aleatorio.

# Finalización

La simulación termina cuando:

No hay suficiente RAM ni SWAP para crear procesos

Se produce una condición crítica (sin frames disponibles)

El creador de procesos finaliza su ciclo
