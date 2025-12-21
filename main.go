package main

import (
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"strconv"
	"time"
)

// Struktur data untuk dikirim ke dashboard
type KPKResponse struct {
	A          int   `json:"A"`
	B          int   `json:"B"`
	KPK        int   `json:"kpk"`
	IterTimeUs int64 `json:"iterTimeUs"`
	RecTimeUs  int64 `json:"recTimeUs"`
}

// Algoritma Iteratif
func kpkIterative(a, b int) int {
	if a == 0 || b == 0 { return 0 }
	hasil := a
	for hasil%b != 0 {
		hasil += a
	}
	return hasil
}

// Algoritma Rekursif
func kpkRecursive(a, b, temp int) int {
	if b == 0 { return 0 }
	if temp%b == 0 { return temp }
	return kpkRecursive(a, b, temp+a)
}

func kpkHandler(w http.ResponseWriter, r *http.Request) {
	// Ambil parameter dari browser
	a, _ := strconv.Atoi(r.URL.Query().Get("a"))
	b, _ := strconv.Atoi(r.URL.Query().Get("b"))

	// OUTPUT TERMINAL - Akan muncul setiap klik "Uji Single"
	fmt.Println("\n==================================================")
	fmt.Printf("[%s] REQUEST MASUK\n", time.Now().Format("15:04:05"))
	fmt.Printf("Input User -> Bilangan A: %d, Bilangan B: %d\n", a, b)

	hasilKPK := kpkIterative(a, b)
	loop := 1_000_000 // Simulasi beban kerja agar running time terlihat

	// Pengujian Iteratif
	startI := time.Now()
	for i := 0; i < loop; i++ {
		kpkIterative(a, b)
	}
	iterTime := time.Since(startI).Microseconds()

	// Pengujian Rekursif
	startR := time.Now()
	for i := 0; i < loop; i++ {
		kpkRecursive(a, b, a)
	}
	recTime := time.Since(startR).Microseconds()

	// Cetak hasil benchmark ke terminal
	fmt.Printf("Hasil KPK: %d\n", hasilKPK)
	fmt.Printf("Running Time Iteratif: %d μs\n", iterTime)
	fmt.Printf("Running Time Rekursif: %d μs\n", recTime)
	fmt.Println("==================================================")

	// Kirim JSON ke frontend
	resp := KPKResponse{
		A: a, B: b, KPK: hasilKPK,
		IterTimeUs: iterTime, RecTimeUs: recTime,
	}
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(resp)
}

func main() {
	// Folder 'static' harus berisi index.html
	http.Handle("/", http.FileServer(http.Dir("static")))
	http.HandleFunc("/kpk/run", kpkHandler)

	fmt.Println("Server berjalan di http://localhost:8080")
	fmt.Println("Menunggu interaksi dari dashboard...")

	// Menggunakan log.Fatal agar terminal tidak langsung tertutup jika error
	log.Fatal(http.ListenAndServe(":8080", nil))
}
