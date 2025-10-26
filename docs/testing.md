# Testing

The project uses CMake to build self-contained test executables. The
recommended workflow is:

1. Configure the build directory:
   ```sh
   cmake -S . -B build
   ```
2. Build all test targets:
   ```sh
   cmake --build build --target \
     config_schema_test \
     world_state_step_order_test \
     systems_behavior_test \
     job_ability_system_test
   ```
3. Run the complete suite with CTest:
   ```sh
   ctest --test-dir build
   ```
   You can filter to a specific executable with `-R`, for example:
   ```sh
   ctest --test-dir build -R job_ability_system
   ```

`world_state_step_order_test` covers the system pipeline ordering, while
`systems_behavior_test` validates formation alignment timers, commander
morale transitions, and spawn pity weighting. `job_ability_system_test`
exercises cooldowns, rally toggles, and spawn-rate boosts in the job
ability system.
