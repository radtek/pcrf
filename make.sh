#!/bin/bash

# Запускаем сборку проекта
make
if [ $? -ne 0 ]
then
  echo "an error occurred while freeDiameter compilation: error code $?"
  exit $?
else
  echo "freeDiameter is compilled successfully"
fi

# Запоминаем pid процесса freeDiameterd
FDPID=$(ps -C freeDiameterd -o pid=)

# Останавливаем pcrf
sudo initctl stop pcrf
if [ $? -ne 0 ]
then
  echo "error occurred while 'initctl stop pcrf' operation: error code $?"
  exit $?
else
  echo "freeDiameter is stopping"
fi

# Ждем окончания его процесса
LIMIT=10
COUNTER=0
while [ "$COUNTER" -lt "$LIMIT" ]
do
  if [ ! -d "/proc/$FDPID" ]
  then
    echo "freeDiameterd is stopped successfully!"
    break
  fi
  sleep 1
  COUNTER=$(($COUNTER+1))
  echo "freeDiameterd is still stopping"
done

if [ "$COUNTER" -eq "$LIMIT" ]
then
  sudo kill -s KILL $FDPID
  echo "freeDiameterd $FDPID is killed by KILL signal"
fi

# Инсталлируем новые библиотеки
sudo make install
if [ $? -ne 0 ]
then
  echo "error occurred while freeDiameter installation: error code $?"
  exit $?
else
  echo "freeDiameter is installed successfully"
fi

# Запускаем pcrf
sudo initctl start pcrf
if [ $? -ne 0 ]
then
  echo "error occurred while freeDiameter launching: error code $?"
  exit $?
else
  echo "freeDiameter is starting"
fi
