package dal

// VTKTransport is an connection interface for VTK device
type VTKTransport interface {
	WriteBytes(bytes ...byte) (int, error)
	ReadBytes([]byte) (int, error)
}
