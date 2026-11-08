#!/usr/bin/env python3
"""Host test: pack a KernelSU module from mock assets and check the ZIP layout."""
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    with tempfile.TemporaryDirectory() as directory:
        d = Path(directory)
        # Mock build directory
        build = d / "build"
        build.mkdir()
        (build / "susfs_ksu_daemon").write_bytes(b"mock_daemon")
        (build / "susfs_ksu_rpc").write_bytes(b"mock_rpc")
        (build / "app-metadata").mkdir()
        (build / "app-metadata" / "app-metadata.dex").write_bytes(b"dex\n035\x00" + b"\x00" * 112)
        import hashlib
        def sha256(path: Path) -> str:
            return hashlib.sha256(path.read_bytes()).hexdigest()
        manifest = {
            "schema_version": 1,
            "project": "susfs4skp-ksu",
            "module": {"id32": "fa362333cf458e832cd032fda9a9f3c6", "version": "0.3.11"},
            "output": {"path": "susfs_ksu_daemon", "bytes": (build / "susfs_ksu_daemon").stat().st_size, "sha256": sha256(build / "susfs_ksu_daemon")},
            "rpc_client": {"path": "susfs_ksu_rpc", "bytes": (build / "susfs_ksu_rpc").stat().st_size, "sha256": sha256(build / "susfs_ksu_rpc")},
            "metadata_dex": {"sha256": sha256(build / "app-metadata" / "app-metadata.dex")},
            "source_files": []
        }
        (build / "build-manifest.json").write_text(json.dumps(manifest), encoding="utf-8")

        # Mock kernel source tree (for receipt validation)
        kernel = ROOT / "kernel"
        if not kernel.exists():
            # Create minimal mock kernel source for test
            kernel.mkdir(parents=True, exist_ok=True)
            (kernel / "Makefile").write_text("# mock\n", encoding="utf-8")
            (kernel / "susfs_main.c").write_text("// mock\n", encoding="utf-8")
            (kernel / "susfs.h").write_text("// mock\n", encoding="utf-8")

        # Create minimal valid ELF64 little-endian AArch64 relocatable object for test KO
        import struct
        def elf_header(machine: int = 183, elf_type: int = 1) -> bytes:
            ident = b"\x7fELF\x02\x01\x01" + b"\x00" * 9
            return ident + struct.pack("<HHIQQQIHHHHHH", elf_type, machine, 1, 0, 0, 0, 0, 64, 0, 0, 0, 0, 0)

        # Mock kernel source tree (for receipt validation)
        kernel = ROOT / "kernel"
        if not kernel.exists():
            # Create minimal mock kernel source for test
            kernel.mkdir(parents=True, exist_ok=True)
            (kernel / "Makefile").write_text("# mock\n", encoding="utf-8")
            (kernel / "susfs_main.c").write_text("// mock\n", encoding="utf-8")
            (kernel / "susfs.h").write_text("// mock\n", encoding="utf-8")

        # Mock LKM directory
        lkm = d / "lkm"
        (lkm / "bin").mkdir(parents=True)
        (lkm / "lib").mkdir()
        kmis = ["android15-6.6", "android16-6.12"]
        # Compute actual source hashes to put in mock receipts
        import hashlib
        def file_sha256(path):
            return hashlib.sha256(path.read_bytes()).hexdigest()
        sources = {source.name: file_sha256(source) for source in kernel.iterdir() if source.is_file() and source.suffix in {".c", ".h"} or source.name == "Makefile"}
        for k in kmis:
            ko_data = elf_header() + k.encode()
            (lkm / "lib" / f"susfs_guard_lkm-{k}.ko").write_bytes(ko_data)
            (lkm / "lib" / f"susfs_guard_lkm-{k}.receipt.json").write_text(json.dumps({
                "variant": k, "status": "complete", "exit_code": 0,
                "ddk_manifest": "a" * 64, "ddk_config": "b" * 64,
                "source": sources, "artifact": {"path": f"mock/susfs_guard_lkm-{k}.ko", "sha256": file_sha256(lkm / "lib" / f"susfs_guard_lkm-{k}.ko")}
            }), encoding="utf-8")
        for name in ("load.sh", "kmi.sh", "kmis", "bin/susfs_insmod"):
            (lkm / name).write_text("fixture\n", encoding="utf-8")
        (lkm / "kmis").write_text("\n".join(kmis) + "\n", encoding="utf-8")

        out = d / "m.zip"
        subprocess.run([sys.executable, str(ROOT / "module/package_ksu.py"),
                        "--build-dir", str(build),
                        "--lkm-dir", str(lkm),
                        "--out", str(out)], check=True)

        with zipfile.ZipFile(out) as z:
            names = set(z.namelist())
            need = {
                "module.prop",
                "bin/susfs_ksu_daemon",
                "bin/susfs_ksu_rpc",
                "app-metadata.dex",
                "early-dispatch.sh", "customize.sh", "service.sh",
                "post-fs-data.sh", "uninstall.sh", "load.sh",
                "susfs4skp-manifest.json",
                "LICENSE"
            } | {f"lkm/{name}" for name in ("load.sh", "kmi.sh", "kmis", "bin/susfs_insmod")} | \
               {f"lkm/lib/susfs_guard_lkm-{k}.ko" for k in kmis} | \
               {f"lkm/lib/susfs_guard_lkm-{k}.receipt.json" for k in kmis} | \
               {f"webroot/{name}" for name in ("index.html", "style.css", "susfs.js", "request.js")}
            assert not need - names, f"missing: {need - names}"
            prop = z.read("module.prop").decode()
            assert "id=susfs_ksu" in prop and "version=0.3.11" in prop, prop
            assert z.read("lkm/kmis").decode().split() == kmis
            # Check manifest format
            manifest_data = json.loads(z.read("susfs4skp-manifest.json").decode())
            assert manifest_data["format"] == "susfs4skp-ksu"
            assert manifest_data["module"]["id32"] == "fa362333cf458e832cd032fda9a9f3c6"
            assert manifest_data["private_directory"] is True
            assert manifest_data["lkm"]["supported_kmis"] == list(kmis)
            assert len(manifest_data["lkm"]["artifacts"]) == 2
            for kmi in kmis:
                assert f"lkm/lib/susfs_guard_lkm-{kmi}.ko" in names
                assert f"lkm/lib/susfs_guard_lkm-{kmi}.receipt.json" in names
        print("KSU pack test ok")


if __name__ == "__main__":
    import json
    main()