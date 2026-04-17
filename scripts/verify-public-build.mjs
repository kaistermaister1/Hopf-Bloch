import { access } from "node:fs/promises";

const requiredFiles = [
  "public/index.html",
  "public/hopf_bloch.js",
  "public/hopf_bloch.wasm",
];

await Promise.all(requiredFiles.map((file) => access(file)));
console.log("Static WebAssembly build is ready in public/.");
