# PBPS_labs

## Установка Redis
~~~
sudo apt update
sudo apt install redis-server
sudo systemctl start redis
sudo apt install libhiredis-dev
~~~

## Заполнение базы данных
В качестве примера заведем трех пользователей. 
~~~
redis-cli
SET user:foxuser foxpassword
SET user:admin adminpass
SET user:test testpass
~~~

# Запуск/остановка проекта
- Запуск
~~~
sudo make install
~~~
- Остановка
~~~
sudo make uninstall
~~~
