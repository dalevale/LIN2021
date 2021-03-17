
for ((i=0; $i<100; i++))
do
  echo add $i > /proc/modlist
  echo Estoy escribiendo $i
  sleep 0.5
done

for ((i=0; $i<50; i++))
do
  clear
  echo remove $i > /proc/modlist
  echo Estoy borrando $i
  cat /proc/modlist
  sleep 1
done
