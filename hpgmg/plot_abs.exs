data_dirs = [
  "~/hpgmg/results/hpgmg_8_16_th_28_iomp/latest",
  "~/hpgmg/results/hpgmg_8_16_th_28_bolt_1000/latest",
  "~/hpgmg/results/hpgmg_8_16_th_28_bolt_10000/latest",
  "~/hpgmg/results/hpgmg_8_16_th_28_bolt_no_preemption/latest",
  "~/hpgmg/results/hpgmg_8_16_th_28_bolt_fixed/latest",
]

opts = [
  {"IOMP"                    , "#1f77b4", "solid"   , "circle"      },
  {"BOLT (preemptive; 1 ms)" , "#ff7f0e", "dashdot" , "square-open" },
  {"BOLT (preemptive; 10 ms)", "#2ca02c", "dot"     , "diamond-open"},
  {"BOLT (nonpreemptive)"    , "#d62728", "longdash", "cross-thin"  },
  {"BOLT (baseline)"         , "#333333", "dash"    , "x-thin"      },
]

# n_workers = :lists.seq(4, 28, 1)
n_workers = :lists.seq(8, 28, 2)

linewidth = 2

get_data_in_file = fn path ->
  [_ | times] =
    File.stream!(path)
    |> Stream.map(&String.split/1)
    |> Stream.map(fn
      ["FMGSolve...", _, _, _, _, "(" <> time, _] -> Float.parse(time) |> elem(0)
      _ -> nil
    end)
    |> Stream.filter(&!is_nil(&1))
    |> Enum.to_list()
    |> Enum.take(11)
  times
end

get_data_in_dir = fn path ->
  Enum.map(n_workers, fn w ->
    times =
      "#{path}/w_#{w}_*.out"
      |> Path.expand()
      |> Path.wildcard()
      |> Enum.map(get_data_in_file)
      |> List.flatten()
    {w, times}
  end)
end

Enum.zip(data_dirs, opts)
|> Enum.map(fn {dir, opt} ->
  {title, color, dash, symbol} = opt
  {xs, times} =
    get_data_in_dir.(dir)
    |> Enum.sort()
    |> Enum.unzip()
  {ys, errors} =
    times
    |> Enum.map(fn t -> {Statistics.mean(t), Statistics.stdev(t)} end)
    |> Enum.unzip()
  %{
    x: xs,
    y: ys,
    name: title,
    error_y: %{
      type:      "data",
      array:     errors,
      visible:   true,
      thickness: linewidth,
    },
    line: %{
      width: linewidth,
      color: color,
      dash: dash,
    },
    marker: %{
      symbol: symbol,
      size:   14,
      line:   %{width: linewidth, color: color},
    },
  }
end)
|> PlotlyEx.plot(%{
  width:  500,
  height: 450,
  xaxis: %{
    showgrid: true,
    title: %{text: "# of workers"},
    showline: true,
  },
  yaxis: %{
    title: %{text: "Elapsed time (s)"},
    showline: true,
    range: [0, 5.5],
  },
  legend: %{
    x: 0,
    y: 0,
    # x: 1,
    # y: 1,
    bgcolor: "rgba(0,0,0,0)",
  },
  font: %{
    size: 22,
  },
  margin: %{
    l: 75,
    r: 10,
    b: 70,
    t: 10,
    pad: 0,
  },
})
|> PlotlyEx.show()
