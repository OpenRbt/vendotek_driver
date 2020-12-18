package main

import (
	"time"
	"vdrive/internal/api"
	"vdrive/internal/app"
	"vdrive/internal/dal"
)

func main() {
	tcpTransport, err := dal.NewTCPTransport("192.168.1.51", "62801")
	if err != nil {
		// return an err code here
	}
	vtkDevice := dal.NewVTK(tcpTransport)
	application := app.NewApplication(vtkDevice)
	ConsoleAPI := api.NewConsoleAPI(application)

	// TODO read the values from cmd parameters
	currencyCode := 652
	mainAmount := 100
	decimalAmount := 0
	timeout := time.Duration(20 * time.Second)

	code := ConsoleAPI.Run(currencyCode, mainAmount, decimalAmount, timeout)
	print(code)
	// return a code properly here
}
