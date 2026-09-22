"""Run the resource catalog contract against a newly initialized disposable MySQL."""
import os
import pathlib
import shutil
import socket
import subprocess
import tempfile
import time
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]


def run():
    mysqld, mysql = shutil.which("mysqld"), shutil.which("mysql")
    if not mysqld or not mysql:
        raise RuntimeError("mysqld and mysql are required; no dependencies will be installed")
    with tempfile.TemporaryDirectory(prefix="resource-mysql-", dir=ROOT / "build/resource") as temporary:
        path = pathlib.Path(temporary)
        data = path / "data"
        subprocess.run([mysqld, "--no-defaults", "--initialize-insecure", f"--datadir={data}",
                        f"--log-error={path / 'mysql.log'}"], check=True, timeout=90)
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0)); port = listener.getsockname()[1]
        process = subprocess.Popen([mysqld, "--no-defaults", f"--datadir={data}",
                                     "--bind-address=127.0.0.1", f"--port={port}", "--mysqlx=0",
                                     f"--log-error={path / 'mysql.log'}"],
                                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        command = [mysql, "--no-defaults", "-h127.0.0.1", f"-P{port}", "-uroot", "--connect-timeout=2"]
        try:
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline:
                result = subprocess.run(command + ["-e", "SELECT 1"], capture_output=True, timeout=5)
                if result.returncode == 0:
                    break
                if process.poll() is not None:
                    raise RuntimeError("isolated MySQL exited; see test initialization output")
                time.sleep(0.1)
            else:
                raise TimeoutError("isolated MySQL not ready")
            subprocess.run(command + ["-e", "CREATE DATABASE resource_test"], check=True, timeout=10)
            migration_env = os.environ.copy()
            migration_env.update(CHAT_MYSQL_CLIENT=mysql, CHAT_MYSQL_HOST="127.0.0.1",
                CHAT_MYSQL_PORT=str(port), CHAT_MYSQL_USER="root", CHAT_MYSQL_PASSWORD="",
                CHAT_MYSQL_DATABASE="resource_test")
            for _ in range(2):
                subprocess.run(["node", str(ROOT / "schema/migrate.js"), "apply"],
                               env=migration_env, check=True, timeout=60)
            env = os.environ.copy(); env["RESOURCE_TEST_MYSQL"] = f"tcp://127.0.0.1:{port}"
            config = path / "dao.ini"
            config.write_text("\n".join([
                "[Mysql]", "Host=127.0.0.1", f"Port={port}", "User=root", "Password=", "Schema=resource_test",
                "[SelfServer]", "Name=dao-fixture", "Host=127.0.0.1", "Port=18080", "RPCPort=18081",
                "[Redis]", "Host=127.0.0.1", "Port=16379",
                "[StatusServer]", "Host=127.0.0.1", "Port=18082",
                "[Log]", "Name=dao-fixture", f"LogDir={path / 'logs'}", ""]), encoding="utf-8")
            env["MYSQL_DAO_TEST_CONFIG"] = str(config)
            subprocess.run([str(ROOT / "build/resource/ResourceTests.exe"),
                            "--gtest_filter=MysqlDaoIntegration.*:ResourceCatalogIntegration.*",
                            f"--gtest_output=xml:{ROOT / 'build/resource/catalog.xml'}"], env=env, check=True, timeout=30)
            if "--catalog-only" not in sys.argv:
                from chat_flow_integration import run_flow
                run_flow(path, command, port)
        except Exception:
            evidence = ROOT / "build/resource/failed-flow"
            evidence.mkdir(parents=True, exist_ok=True)
            for log in list(path.glob("*.stdout")) + list((path / "logs").rglob("*")):
                if log.is_file():
                    shutil.copyfile(log, evidence / log.name)
            raise
        finally:
            subprocess.run(command + ["-e", "SHUTDOWN"], capture_output=True, timeout=8)
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.terminate(); process.wait(timeout=10)


if __name__ == "__main__":
    run()
