package main

import (
	"flag"
	"fmt"
	"io"
	"log"
	"os"
	"os/exec"
)

func EmitCFile(prefix string, w io.Writer) {
	fmt.Fprintf(w, "extern char %s_base;\n", prefix)
	fmt.Fprintf(w, "extern char %s_end;\n", prefix)
	fmt.Fprintf(w, "extern char %s_entry;\n", prefix)
}

func GenSym(prefix string, file string) {
	fc, err := os.Create("lib.c")
	if err != nil {
		log.Fatal(err)
	}
	EmitCFile(prefix, fc)
	fc.Close()
}

func Run(command string, args ...string) {
	cmd := exec.Command(command, args...)
	log.Println(cmd)
	cmd.Stderr = os.Stderr
	cmd.Stdout = os.Stdout
	cmd.Stdin = os.Stdin
	err := cmd.Run()
	if err != nil {
		log.Fatal(err)
	}
}

func main() {
	prefix := flag.String("prefix", "lib", "library name to prefix on all symbols")
	genSym := flag.Bool("gen-sym", false, "generate symbols file")

	flag.Parse()
	args := flag.Args()

	if *genSym {
		GenSym(*prefix, "lib.c")
		return
	}

	stub := args[0]
	hostso := args[1]
}
