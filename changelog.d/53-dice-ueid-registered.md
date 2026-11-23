### Security

- VerifyDICEAttestation takes a DICERegistry and refuses a UEID that is not registered, or whose registered DeviceID key is not x5c[1]'s key, so another device's DeviceID under the same root cannot claim it.
