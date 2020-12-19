package main

import (
	"errors"
	"fmt"
	"os"
	"time"
	"unicode"
	"vdrive/internal/api"
	"vdrive/internal/app"
	"vdrive/internal/dal"
)

var (
	wrongParametersCode = 254
)

// Errors...
var (
	ErrCantParse = errors.New("can't parse parameter")
)

func main() {
	// Let's parse command line arguments
	args := os.Args[1:]
	if len(args) != 3 {
		fmt.Print("bad cmd params\n")
		os.Exit(wrongParametersCode)
	}

	currencyCode, err := unpackCurrencyCode(args[2])
	if err != nil {
		fmt.Printf("can't use this as currency code (must be like c643):%s\n", args[2])
		os.Exit(wrongParametersCode)
	}

	decimalAmount, err := unpackMoneyAmount(args[1])
	if err != nil {
		fmt.Printf("can't use this as currency code (must be like c643):%s\n", args[2])
		os.Exit(wrongParametersCode)
	}

	tcpTransport, err := dal.NewTCPTransport("192.168.1.51", "62801")
	if err != nil {
		// return an err code here
	}

	vtkDevice := dal.NewVTK(tcpTransport)
	application := app.NewApplication(vtkDevice)
	ConsoleAPI := api.NewConsoleAPI(application)

	// TODO read the values from cmd parameters

	timeout := time.Duration(20 * time.Second)

	code := ConsoleAPI.Run(currencyCode, decimalAmount, timeout)
	os.Exit(code)
	// return a code properly here
	return
}

func unpackCurrencyCode(parameter string) (int, error) {
	return unpackParameter(parameter, 'c')
}

func unpackMoneyAmount(parameter string) (int, error) {
	return unpackParameter(parameter, 'a')
}

func unpackParameter(parameter string, mustStartWithLetter rune) (int, error) {
	result := 0
	for i, r := range parameter {
		// Let's process first symbol carefully
		if i == 0 {
			if r != mustStartWithLetter {
				return -1, ErrCantParse
			}
			continue
		}
		if !unicode.IsNumber(r) {
			return -1, ErrCantParse
		}
		result = result*10 + int(r) - int('0')
	}
	return result, nil
}
