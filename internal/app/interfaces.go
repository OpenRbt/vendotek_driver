package app

import "time"

// Application describes an interface for the app
type Application interface {
	RequestMoney(currencyCode, mainAmount, decimalAmount int, timeout time.Duration) error
}

// CardReader describes an interface for the card reader
type CardReader interface {
	RequestMoney(currencyCode, mainAmount, decimalAmount int) error
}

// API describes an API for the driver, returns error code
type API interface {
	Run(currencyCode, mainAmount, decimalAmount int, timeout time.Duration) int
}
