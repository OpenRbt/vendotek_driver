package app

import "time"

// App is a struct of Application implementation
type App struct {
	Device CardReader
}

// NewApplication is a constructor
func NewApplication(newDevice CardReader) Application {
	res := &App{
		Device: newDevice,
	}
	return res
}

// RequestMoney requests the money
func (a *App) RequestMoney(currencyCode, mainAmount, decimalAmount int, timeout time.Duration) error {
	// TODO consider timeout
	err := a.Device.RequestMoney(currencyCode, mainAmount, decimalAmount)
	return err
}
