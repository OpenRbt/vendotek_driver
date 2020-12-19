package app

import "time"

// Application describes an interface for the app
type Application interface {
	RequestMoney(currencyCode, decimalAmount int, timeout time.Duration) error
}

// CardReader describes an interface for the card reader
type CardReader interface {
	RequestMoney(currencyCode, decimalAmount int) error
}

// API describes an API for the driver, returns error code
type API interface {
	Run(currencyCode, decimalAmount int, timeout time.Duration) int
}
