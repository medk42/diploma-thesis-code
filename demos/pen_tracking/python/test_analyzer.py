import pandas as pd
import matplotlib.pyplot as plt

df = pd.read_csv("demos/pen_tracking/data/test_results/test_laptop__2025_03_20__17_37.csv")
df_filtered = df[df['with_visualization'] == 1]

fig, axs = plt.subplots(2, 2, figsize=(10,10))
axs[0, 0].plot(df_filtered['search_window_perc'], df_filtered['time_min_ms'], label='time_min_ms')
axs[0, 0].plot(df_filtered['search_window_perc'], df_filtered['time_max_ms'], label='time_max_ms')
axs[0, 0].set_xlabel('search_window_perc')
axs[0, 0].set_ylabel('time_min_ms, time_max_ms')
axs[0, 0].set_title('Time Min and Max')
axs[0, 0].legend()
for x in df_filtered['search_window_perc'].unique():
    axs[0, 0].axvline(x=x, linestyle='--', alpha=0.2, color='gray', zorder=0)

axs[0, 1].plot(df_filtered['search_window_perc'], df_filtered['time_percentile_01_ms'], label='time_percentile_01_ms')
axs[0, 1].plot(df_filtered['search_window_perc'], df_filtered['time_percentile_1_ms'], label='time_percentile_1_ms')
axs[0, 1].plot(df_filtered['search_window_perc'], df_filtered['time_percentile_10_ms'], label='time_percentile_10_ms')
axs[0, 1].plot(df_filtered['search_window_perc'], df_filtered['time_percentile_25_ms'], label='time_percentile_25_ms')
axs[0, 1].plot(df_filtered['search_window_perc'], df_filtered['time_percentile_50_ms'], label='time_percentile_50_ms')
axs[0, 1].set_xlabel('search_window_perc')
axs[0, 1].set_ylabel('time_percentiles')
axs[0, 1].set_title('Time Percentiles')
axs[0, 1].legend()
for x in df_filtered['search_window_perc'].unique():
    axs[0, 1].axvline(x=x, linestyle='--', alpha=0.2, color='gray', zorder=0)

axs[1, 0].plot(df_filtered['search_window_perc'], df_filtered['time_avg_ms'], label='time_avg_ms')
axs[1, 0].plot(df_filtered['search_window_perc'], df_filtered['time_std_ms'], label='time_std_ms')
axs[1, 0].set_xlabel('search_window_perc')
axs[1, 0].set_ylabel('time_avg_ms, time_std_ms')
axs[1, 0].set_title('Time Average and Standard Deviation')
axs[1, 0].legend()
for x in df_filtered['search_window_perc'].unique():
    axs[1, 0].axvline(x=x, linestyle='--', alpha=0.2, color='gray', zorder=0)

axs[1, 1].plot(df_filtered['search_window_perc'], df_filtered['failure_perc'], label='failure_perc')
axs[1, 1].plot(df_filtered['search_window_perc'], df_filtered['lost_tracking_perc']*10, label='lost_tracking_perc')
axs[1, 1].set_xlabel('search_window_perc')
axs[1, 1].set_ylabel('failure_perc, lost_tracking_perc')
axs[1, 1].set_title('Failure and Lost Tracking Percentages')
axs[1, 1].set_ylim(0, 0.1)
axs[1, 1].legend()
for x in df_filtered['search_window_perc'].unique():
    axs[1, 1].axvline(x=x, linestyle='--', alpha=0.2, color='gray', zorder=0)
plt.tight_layout()
plt.show()