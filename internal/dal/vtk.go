package dal

import "vdrive/internal/app"

// VTK is a code to communicate to device with TCP
type VTK struct {
	Transport VTKTransport
}

// NewVTK is a constructor for VTK device
func NewVTK(newTransport VTKTransport) app.CardReader {
	res := &VTK{
		Transport: newTransport,
	}
	return res
}

// RequestMoney is just to implement CardReader interface. It requests the money
func (v *VTK) RequestMoney(currencyCode, mainAmount, decimalAmount int) error {
	// TODO Put Reads and Writes accodrdingly to the VTK protocol
	// v.Transport.WriteBytes() - something like that
	return nil
}
