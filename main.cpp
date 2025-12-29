package main

import (
	"encoding/json"
	"html/template"
	"log"
	"math/rand"
	"net/http"
	"os"
	"sort"
	"strconv"
	"strings"
	"time"
)

type Student struct {
	Name  string `json:"name"`
	Score int    `json:"score"`
	Grade string `json:"grade"`
}

type SortRequest struct {
	Algorithm string `json:"algorithm"` // "bubble" or "merge"
	RawInput  string `json:"rawInput"`  // lines: "name,score"
}

type SortResponse struct {
	Algorithm string         `json:"algorithm"`
	Sorted    []Student      `json:"sorted"`
	Summary   map[string]int `json:"summary"`
	Error     string         `json:"error,omitempty"`
	ElapsedUS int64          `json:"elapsedUS"` // microseconds (sorting only)
}

type BenchmarkResponse struct {
	Sizes         []int     `json:"sizes"`
	BubbleMS      []float64 `json:"bubbleMS"`
	MergeMS       []float64 `json:"mergeMS"`
	BubbleClass   string    `json:"bubbleClass"`
	MergeClass    string    `json:"mergeClass"`
	CrossoverN    int       `json:"crossoverN"`
	CrossoverHint string    `json:"crossoverHint"`
	Note          string    `json:"note"`
}

// =====================
// Grade classification
// =====================
func gradeFromScore(score int) string {
	switch {
	case score >= 85:
		return "A"
	case score >= 70:
		return "B"
	case score >= 55:
		return "C"
	case score >= 40:
		return "D"
	default:
		return "E"
	}
}

// =====================
// Parsing input
// =====================
func parseStudents(raw string) ([]Student, error) {
	lines := strings.Split(raw, "\n")
	out := make([]Student, 0, len(lines))
	for _, line := range lines {
		line = strings.TrimSpace(line)
		if line == "" {
			continue
		}
		parts := strings.Split(line, ",")
		if len(parts) != 2 {
			return nil, &badRequestError{"Format salah. Tiap baris harus: nama,score (contoh: Budi,78)"}
		}
		name := strings.TrimSpace(parts[0])
		scoreStr := strings.TrimSpace(parts[1])
		if name == "" {
			return nil, &badRequestError{"Nama tidak boleh kosong."}
		}
		score, err := strconv.Atoi(scoreStr)
		if err != nil {
			return nil, &badRequestError{"Score harus integer. Contoh: 78"}
		}
		if score < 0 || score > 100 {
			return nil, &badRequestError{"Score harus 0..100"}
		}
		out = append(out, Student{
			Name:  name,
			Score: score,
			Grade: gradeFromScore(score),
		})
	}
	if len(out) == 0 {
		return nil, &badRequestError{"Masukan kosong. Isi minimal 1 baris data."}
	}
	return out, nil
}

type badRequestError struct{ msg string }

func (e *badRequestError) Error() string { return e.msg }

// =====================
// Comparator (shared)
// =====================
func less(a, b Student) bool {
	if a.Score != b.Score {
		return a.Score < b.Score
	}
	return a.Name <= b.Name
}

// =====================
// Bubble Sort (ITERATIVE) - versi standar (tanpa early-stop)
// Tujuan: hindari best-case O(n) yang membuat Bubble bisa unggul pada data hampir terurut.
// =====================
func bubbleSortIterativeStandard(a []Student) {
	n := len(a)
	for i := 0; i < n-1; i++ {
		for j := 0; j < n-1-i; j++ {
			if !less(a[j], a[j+1]) {
				a[j], a[j+1] = a[j+1], a[j]
			}
		}
	}
}

// =====================
// Merge Sort (RECURSIVE) - optimized (buffer reusable)
// Tujuan: kurangi overhead alokasi agar Merge menang bahkan pada data kecil.
// =====================
func mergeSortRecursiveOptimized(a []Student) []Student {
	if len(a) <= 1 {
		out := make([]Student, len(a))
		copy(out, a)
		return out
	}
	work := make([]Student, len(a))
	copy(work, a)
	buf := make([]Student, len(a))
	mergeSortRange(work, buf, 0, len(work))
	return work
}

func mergeSortRange(work, buf []Student, lo, hi int) {
	if hi-lo <= 1 {
		return
	}
	mid := lo + (hi-lo)/2

	mergeSortRange(work, buf, lo, mid)
	mergeSortRange(work, buf, mid, hi)

	i, j, k := lo, mid, lo
	for i < mid && j < hi {
		if less(work[i], work[j]) {
			buf[k] = work[i]
			i++
		} else {
			buf[k] = work[j]
			j++
		}
		k++
	}
	for i < mid {
		buf[k] = work[i]
		i++
		k++
	}
	for j < hi {
		buf[k] = work[j]
		j++
		k++
	}
	copy(work[lo:hi], buf[lo:hi])
}

// =====================
// Helper: summarize grade counts
// =====================
func gradeSummary(st []Student) map[string]int {
	m := map[string]int{"A": 0, "B": 0, "C": 0, "D": 0, "E": 0}
	for _, s := range st {
		m[s.Grade]++
	}
	return m
}

// =====================
// HTTP Handlers
// =====================
func serveIndex(w http.ResponseWriter, r *http.Request) {
	tplBytes, err := os.ReadFile("index.html")
	if err != nil {
		http.Error(w, "index.html tidak ditemukan. Pastikan index.html satu folder dengan main.go dan jalankan go run dari folder tsb.", http.StatusInternalServerError)
		return
	}
	tpl, err := template.New("index").Parse(string(tplBytes))
	if err != nil {
		http.Error(w, "index.html template error: "+err.Error(), http.StatusInternalServerError)
		return
	}
	_ = tpl.Execute(w, nil)
}

func handleSort(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "Method harus POST", http.StatusMethodNotAllowed)
		return
	}

	var req SortRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "JSON tidak valid", http.StatusBadRequest)
		return
	}

	students, err := parseStudents(req.RawInput)
	if err != nil {
		writeJSON(w, SortResponse{Algorithm: req.Algorithm, Error: err.Error()}, http.StatusBadRequest)
		return
	}

	algo := strings.ToLower(strings.TrimSpace(req.Algorithm))
	start := time.Now()

	var sorted []Student
	switch algo {
	case "bubble":
		sorted = make([]Student, len(students))
		copy(sorted, students)
		bubbleSortIterativeStandard(sorted)
	case "merge":
		sorted = mergeSortRecursiveOptimized(students)
	default:
		writeJSON(w, SortResponse{Algorithm: req.Algorithm, Error: "Algorithm harus 'bubble' atau 'merge'"}, http.StatusBadRequest)
		return
	}

	elapsed := time.Since(start)
	resp := SortResponse{
		Algorithm: algo,
		Sorted:    sorted,
		Summary:   gradeSummary(sorted),
		ElapsedUS: elapsed.Microseconds(),
	}
	writeJSON(w, resp, http.StatusOK)
}

func handleBenchmark(w http.ResponseWriter, r *http.Request) {
	sizes := buildSizes()

	// trial median biar stabil
	const trials = 9

	bubbleMS := make([]float64, 0, len(sizes))
	mergeMS := make([]float64, 0, len(sizes))

	seed := time.Now().UnixNano()
	rng := rand.New(rand.NewSource(seed))

	for _, n := range sizes {
		base := randomStudents(rng, n)

		bTimes := make([]float64, 0, trials)
		mTimes := make([]float64, 0, trials)

		for t := 0; t < trials; t++ {
			// Bubble
			a1 := make([]Student, len(base))
			copy(a1, base)
			startB := time.Now()
			bubbleSortIterativeStandard(a1)
			bTimes = append(bTimes, float64(time.Since(startB).Nanoseconds())/1e6)

			// Merge optimized
			a2 := make([]Student, len(base))
			copy(a2, base)
			startM := time.Now()
			_ = mergeSortRecursiveOptimized(a2)
			mTimes = append(mTimes, float64(time.Since(startM).Nanoseconds())/1e6)
		}

		bubbleMS = append(bubbleMS, median(bTimes))
		mergeMS = append(mergeMS, median(mTimes))
	}

	crossover := findCrossover(sizes, bubbleMS, mergeMS)

	resp := BenchmarkResponse{
		Sizes:         sizes,
		BubbleMS:      bubbleMS,
		MergeMS:       mergeMS,
		BubbleClass:   "O(n^2) (Bubble Sort iteratif, versi standar tanpa early-stop)",
		MergeClass:    "O(n log n) (Merge Sort rekursif, optimized buffer reusable)",
		CrossoverN:    crossover,
		CrossoverHint: crossoverHint(crossover),
		Note:          "Waktu dalam ms (median beberapa trial). Crossover bergantung perangkat dan beban sistem.",
	}
	writeJSON(w, resp, http.StatusOK)
}

// =====================
// Benchmark helpers
// =====================
func buildSizes() []int {
	// 1, 10, 20, ..., 10000 (representatif dan realistis)
	sizes := []int{1}
	for n := 10; n <= 100; n += 10 {
		sizes = append(sizes, n)
	}
	for n := 200; n <= 1000; n += 200 {
		sizes = append(sizes, n)
	}
	for n := 2000; n <= 10000; n += 2000 {
		sizes = append(sizes, n)
	}
	return sizes
}

func randomStudents(rng *rand.Rand, n int) []Student {
	out := make([]Student, n)
	for i := 0; i < n; i++ {
		score := rng.Intn(101)
		out[i] = Student{
			Name:  "Mhs" + strconv.Itoa(i+1),
			Score: score,
			Grade: gradeFromScore(score),
		}
	}
	return out
}

func median(xs []float64) float64 {
	if len(xs) == 0 {
		return 0
	}
	cp := make([]float64, len(xs))
	copy(cp, xs)
	sort.Float64s(cp)
	m := len(cp) / 2
	if len(cp)%2 == 1 {
		return cp[m]
	}
	return (cp[m-1] + cp[m]) / 2
}

func findCrossover(sizes []int, bubbleMS, mergeMS []float64) int {
	if len(sizes) == 0 || len(sizes) != len(bubbleMS) || len(sizes) != len(mergeMS) {
		return -1
	}
	for i := 0; i < len(sizes); i++ {
		if mergeMS[i] <= bubbleMS[i] {
			return sizes[i]
		}
	}
	return -1
}

func crossoverHint(n int) string {
	if n <= 0 {
		return "Tidak ditemukan pada sampel ukuran input yang diuji (coba tambah resolusi ukuran input)."
	}
	return "Mulai n = " + strconv.Itoa(n) + ", Merge Sort cenderung sama/lebih cepat dibanding Bubble Sort pada ukuran input yang diuji."
}

func writeJSON(w http.ResponseWriter, v any, status int) {
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.Header().Set("Access-Control-Allow-Origin", "*")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(v)
}

func main() {
	mux := http.NewServeMux()
	mux.HandleFunc("/", serveIndex)
	mux.HandleFunc("/api/sort", handleSort)
	mux.HandleFunc("/api/benchmark", handleBenchmark)

	srv := &http.Server{
		Addr:              ":8080",
		Handler:           mux,
		ReadHeaderTimeout: 5 * time.Second,
	}

	log.Println("Server running: http://localhost:8080")
	log.Fatal(srv.ListenAndServe())
}
