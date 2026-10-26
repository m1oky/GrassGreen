!database!
* Конфигурация профиля и подсистем GrassGreen

table(auth)
user(m1oky)
user_role(developer)
last_log(auto)
remember_me(1)
auth_token_hash(auto)
db_encrypt(auth_token_hash)
end-table

table(network)
net_tunnel_protocol(VLESS-Reality)
net_baypass_state(1)
net_gateway_route(127.0.0.1:10808)
net_bypass_config(vless_reality.json)
net_ping_ms(18)
net_dns_server(1.1.1.1)
end-table

table(runtime)
env_target_runtime(fabric-1.20.1)
env_allocated_ram(4096)
env_exec_flags(-XX:+UseG1GC -XX:+UnlockExperimentalVMOptions)
env_java_path(C:/Program Files/Java/bin/javaw.exe)
sys_fps_stabilizer(1)
sys_gpu_force_integrated(1)
end-table

* Экспортируем копию настроек в JSON и сохраняем
db_export_json(config_dump.json)