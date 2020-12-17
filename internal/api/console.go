package api

import (
	"time"
	"vdrive/internal/app"
)

// ConsoleAPI is an API for vendotek
type ConsoleAPI struct {
	App app.Application
}

// NewConsoleAPI is a constructor of API
func NewConsoleAPI(app app.Application) app.API {
	res := &ConsoleAPI{}
	return res
}

// Run is to implement an interface, returning an error code
func (c *ConsoleAPI) Run(currencyCode, mainAmount, decimalAmount int, timeout time.Duration) int {
	err := c.App.RequestMoney(currencyCode, mainAmount, decimalAmount, timeout)
	if err != nil {
		// TODO process an error here
	}
	return 0

	// Let's connect to TCP here
}
