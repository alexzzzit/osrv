make build

echo "Шифруем input.txt -> output.txt"
./otp -i input.txt -o output.txt -x 4212 -a 84589 -c 45989 -m 217728

echo "Дешифруем output.txt -> reinput.txt"
./otp -i output.txt -o reinput.txt -x 4212 -a 84589 -c 45989 -m 217728

echo "Сравниваем input.txt и reinput.txt:"
diff input.txt reinput.txt

if [ $? -eq 0 ]; then
	echo "Тест пройден успешно: файлы идентичны"
	exit 0
else
	echo "Ошибка: файлы различаются"
	exit 1
fi
