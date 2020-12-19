package api

import (
	"fmt"
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
	res.App = app
	return res
}

// Run is to implement an interface, returning an error code
func (c *ConsoleAPI) Run(currencyCode, decimalAmount int, timeout time.Duration) int {
	fmt.Printf("trying to charge [%d] *1/100, of currency core [%d]\n", decimalAmount, currencyCode)
	err := c.App.RequestMoney(currencyCode, decimalAmount, timeout)

	if err != nil {
		// TODO process an error here
	}
	return 0

	// Let's connect to TCP here
}
