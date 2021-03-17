Dale Francis Valencia Calicdan

El programa ofrece al usuario, crear/eliminar entradas en la entrada /proc/multipc escribiendo en la entrada /admin. También ofreceel servicio 
de crear hilos que lee/escribe en la entrada <proc_entry_name> especificado. El número de hilos que se pueden crear (de lectura o escritura)
son 6.

Uso: ./user <options -n/-e/-d> <proc_entry_name> <type s/i> [<-r/-w> <segundos>]

-n - Crear una nueva entrada con nombre <proc_entry_name> de tipo cadenas de carácteres o enteros <s/i>.
-d - Eliminar la entrada con el nombre <proc_entry_name>.
-e - Ejecutar lectura/escritura creando los hilos especificados con -r/-w.

s  - Tipo cadenas de carácteres. En la opción -e, el hilo de escritura esrcibirá cadenas de carácteres implementado en el código.
	 En este caso son los capitales de algunos paises europeos.
i  - Tipo enteros. En la opción -e, el hilo de escritura creado, escribirá numeros enteros. En este caso son el numero de 1 a 8.

-r - Crear un hilo de lectura que lee de la entrada <proc_entry_name> en un intervalo de <segundos>.
-w - Crear un hilo de escritura que esribe en la entrada <proc_entry_name> en un intervalo de <segundos>.